import os
import time
import subprocess
import logging

logger = logging.getLogger(__name__)


def all_same(items):
    return all(x == items[0] for x in items)


class McuMgrExecutor:
    def __init__(
        self,
        conn=None,
        conn_string=None,
        conn_type=None,
        timeout=60,
        smp_serial_obj=None,
        run_root=False,
    ):
        self.conn = conn
        self.conn_string = conn_string
        self.conn_type = conn_type
        self.timeout = timeout
        self.run_root = run_root
        # get mcumgr path for independence of environment when running root
        self.mcumgr_path = self.get_mcumgr_path()

        if "mtu" in conn_string:
            for i, s in enumerate(conn_string.split(",")):
                if "mtu" in s:
                    self.mtu = int(s.split("=")[1])

    def set_conn(self, conn):
        self.conn = conn

    def set_conn_string(self, conn_string):
        self.conn_string = conn_string

    def set_conn_type(self, conn_type):
        self.conn_type = conn_type

    def get_mtu(self):
        if self.mtu is None:
            return 512  # returning 512 as default MTU
        return self.mtu

    def check_line_repeats(self, lines, new_line, max_repeats, time=None):
        # adds "new_line" to "kubes"
        # returns true if the "lines" have repeated "max_repeats" times
        curr_lines_length = len(lines)
        lines.append(new_line)
        if curr_lines_length < max_repeats - 1:
            return False
        else:
            lines.pop(0)
            return all_same(lines)

    def get_mcumgr_path(self):
        process = subprocess.run("which mcumgr", capture_output=True, shell=True)
        mcumgr_path = process.stdout.decode().strip()
        return mcumgr_path

    def execute_mcumgr_cmd(self, mcu_cmd, extra_args=None, max_repeat=None):
        """
        mcu_cmd: mcumgr support command
        extra_args: extra arguments string to send with mcumgr command
        max_repeat: maximum number of stdout lines that can repeat in a row
        """
        command = ""
        if self.run_root:
            command += "sudo"

        command += f" {self.mcumgr_path} {mcu_cmd}"
        if extra_args:
            command += " " + extra_args
        if self.conn:
            command += f" --conn {self.conn}"
        if self.conn_string:
            command += f" --connstring {self.conn_string}"
        if self.conn_type:
            command += f" --conntype {self.conn_type}"

        command += f" -t {self.timeout}"
        logger.debug(f"Executing command '{command}'")
        stdout = ""

        num_retries = 0
        command_success = False
        if max_repeat:
            last_lines = []
        while num_retries < 10 and (not command_success):
            try:
                logger.debug(f"Num Command Retries: {num_retries}")
                process = subprocess.Popen(
                    [command],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    shell=True,
                    universal_newlines=True,
                )
                for stdout_line in iter(process.stdout.readline, ""):
                    stdout += stdout_line
                    logger.debug(stdout_line)
                    if max_repeat and self.check_line_repeats(
                        last_lines, stdout_line, max_repeat
                    ):
                        process.terminate()
                        last_lines = []
                return_code = process.wait()
                stderr = process.stderr.read()
                command_success = return_code == 0
                num_retries += 1
                # ensure process is terminated
                process.terminate()
            except Exception as err:
                logger.error(err)
        logger.debug(stderr)
        return command_success, stdout, stderr

    # mcumgr commands

    def shell_command(self, shell_exec_cmd, extra_args=None):
        shell_cmd = f"shell exec {shell_exec_cmd}"
        ret_status, output, error = self.execute_mcumgr_cmd(shell_cmd, extra_args)
        if error:
            logger.error(error)
        return ret_status, output

    def fs_command(self, fs_exec_cmd, extra_args=None):
        fs_cmd = f"fs {fs_exec_cmd}"
        return self.execute_mcumgr_cmd(fs_cmd, extra_args)

    def image_command(self, image_exec_cmd, extra_args=None, max_repeat=None):
        image_cmd = f"image {image_exec_cmd}"
        return self.execute_mcumgr_cmd(image_cmd, extra_args, max_repeat)

    def reset_command(self, extra_args=None):
        reset_cmd = f"reset"

        ret_status, _, se = self.execute_mcumgr_cmd(reset_cmd, extra_args)
        if se:
            logger.error(se)

        return ret_status

    # fs Commands

    def upload_file(self, src, dst, extra_args=None):
        upload_cmd = f"upload {src} {dst}"

        return self.fs_command(upload_cmd, extra_args)

    def download_file(self, src, dst, extra_args=None):
        download_cmd = f"download {src} {dst}"

        return self.fs_command(download_cmd, extra_args)

    # image commands
    def upload_image(self, image_file, extra_args=None, max_repeat=None):
        abs_path_to_image = os.path.abspath(image_file)
        upload_cmd = f"upload {abs_path_to_image}"
        return self.image_command(upload_cmd, extra_args, max_repeat)

    def list_image(self, extra_args=None):
        list_cmd = f"list"
        return self.image_command(list_cmd, extra_args)

    def test_image(self, image_hash, extra_args=None):
        test_cmd = f"test {image_hash}"
        return self.image_command(test_cmd, extra_args)

    def confirm_image(self, image_hash, extra_args=None):
        confirm_cmd = f"confirm {image_hash}"
        return self.image_command(confirm_cmd, extra_args)

    def erase_image(self, extra_args=None):
        erase_cmd = f"erase"
        return self.image_command(erase_cmd, extra_args)

    def get_image_hash(self, image_id, slot_id):
        ret_status, so, se = self.list_image()
        if not ret_status:
            logger.error(se)
            return None

        found_image_info = False
        for line in so.splitlines():
            if f"image={image_id}" in line and f"slot={slot_id}" in line:
                found_image_info = True

            if found_image_info and "hash" in line:
                return line.split(":")[1].strip()
