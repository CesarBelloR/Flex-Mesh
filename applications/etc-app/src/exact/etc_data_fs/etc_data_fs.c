#include <stdio.h>
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <assert.h>
#include <string.h>
#include <zephyr/zephyr.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DATA_FS_LOG_LEVEL);

#define MAX_PATH_LEN 64
#define MAX_FILE_NUMERAL 9999
#define FILE_NUMERAL_LEN 4
#define LOG_PREFIX_LEN (sizeof(CONFIG_DATA_FS_FILE_PREFIX) - 1)

enum data_fs_state {
	DATA_FS_NOT_INITIALIZED = 0,
	DATA_FS_CORRUPTED,
	DATA_FS_OK
};

static struct fs_file_t file;
static enum data_fs_state data_state = DATA_FS_NOT_INITIALIZED;
static int file_ctr, newest, oldest;
static char file_name[MAX_PATH_LEN];
static int allocate_new_file(struct fs_file_t *file);
static int del_oldest_log(void);
static int get_log_file_id(struct fs_dirent *ent);

static int check_log_volumen_available(void)
{
	int index = 0;
	char const *name;
	int rc = 0;

	while (rc == 0) {
		rc = fs_readmount(&index, &name);
		LOG_INF("fs_readmount %d %s", rc, name);
		if (rc == 0) {
			if (strncmp(CONFIG_DATA_FS_DIR, name, strlen(name))
			    == 0) {
				return 0;
			}
		}
	}
	return -ENOENT;
}

static int create_log_dir(const char *path)
{
	const char *next;
	const char *last = path + (strlen(path) - 1);
	char w_path[MAX_PATH_LEN];
	int rc, len;
	struct fs_dir_t dir;

	fs_dir_t_init(&dir);

	/* Check if directory already exists. If yes, return early. */
	rc = fs_opendir(&dir, path);
	if (rc == 0) {
		fs_closedir(&dir);
		return 0;
	}

	/* the fist directory name is the mount point*/
	/* the firs path's letter might be meaningless `/`, let's skip it */
	next = strchr(path + 1, '/');
	if (!next) {
		return 0;
	}

	while (true) {
		next++;
		if (next > last) {
			return 0;
		}
		next = strchr(next, '/');
		if (!next) {
			next = last;
			len = last - path + 1;
		} else {
			len = next - path;
		}

		memcpy(w_path, path, len);
		w_path[len] = 0;

		rc = fs_opendir(&dir, w_path);
		if (rc) {
			/* assume directory doesn't exist */
			rc = fs_mkdir(w_path);
			if (rc) {
				break;
			}
		}
		rc = fs_closedir(&dir);
		if (rc) {
			break;
		}
	}

	return rc;

}

static int check_log_file_exist(int num)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	int rc;

	fs_dir_t_init(&dir);

	rc = fs_opendir(&dir, CONFIG_DATA_FS_DIR);
	if (rc) {
		return -EIO;
	}

	while (true) {
		rc = fs_readdir(&dir, &ent);
		if (rc < 0) {
			rc = -EIO;
			goto close_dir;
		}
		if (ent.name[0] == 0) {
			break;
		}

		rc = get_log_file_id(&ent);

		if (rc == num) {
			rc = 1;
			goto close_dir;
		}
	}

	rc = 0;

close_dir:
	(void) fs_closedir(&dir);

	return rc;
}

static int write_log_to_file(uint8_t *data, size_t length)
{
	int rc;
	struct fs_file_t *f = &file;
	
	if (data_state == DATA_FS_NOT_INITIALIZED) {
		if (check_log_volumen_available()) {
			return length;
		}
		
		rc = create_log_dir(CONFIG_DATA_FS_DIR);
		if (!rc) {
			rc = allocate_new_file(&file);
		}
		
		data_state = (rc ? DATA_FS_CORRUPTED : DATA_FS_OK);
	}

	if (data_state == DATA_FS_OK) {

		/* Check if new data overwrites max file size.
		 * If so, create new log file.
		 */
		int size = fs_tell(f);
		
		if (size < 0) {
			data_state = DATA_FS_CORRUPTED;

			return length;
		} else if ((size + length) > CONFIG_DATA_FS_FILE_SIZE) {
			rc = allocate_new_file(f);

			if (rc < 0) {
				goto on_error;
			}
		}

		rc = fs_write(f, data, length);
		if (rc >= 0) {
			if (IS_ENABLED(CONFIG_DATA_FS_OVERWRITE) &&
			    (rc != length)) {
				del_oldest_log();

				return 0;
			}
			/* If overwrite is disabled, full memory
			 * cause the log record abandonment.
			 */
			length = rc;
		} else {
			rc = check_log_file_exist(newest);
			if (rc == 0) {
				/* file was lost somehow
				 * try to get a new one
				 */
				file_ctr--;
				rc = allocate_new_file(f);
				if (rc < 0) {
					goto on_error;
				}
			} else if (rc < 0) {
				/* fs is corrupted*/
				goto on_error;
			}
			length = 0;
		}
		
		rc = fs_sync(f);
		if (rc < 0) {
			/* Something is wrong */
			goto on_error;
		}
	}
	
	return length;

on_error:
	data_state = DATA_FS_CORRUPTED;
	return length;
}

static int get_log_file_id(struct fs_dirent *ent)
{
	size_t len;
	int num;

	if (ent->type != FS_DIR_ENTRY_FILE) {
		return -1;
	}

	len = strlen(ent->name);

	if (len != LOG_PREFIX_LEN + FILE_NUMERAL_LEN) {
		return -1;
	}

	if (memcmp(ent->name, CONFIG_DATA_FS_FILE_PREFIX, LOG_PREFIX_LEN) != 0) {
		return -1;
	}

	num = atoi(ent->name + LOG_PREFIX_LEN);

	if (num <= MAX_FILE_NUMERAL && num >= 0) {
		return num;
	}

	return -1;
}

static int allocate_new_file(struct fs_file_t *file)
{
	/* In case of no log file or current file fills up
	 * create new log file.
	 */
	int rc;
	struct fs_statvfs stat;
	int curr_file_num;
	struct fs_dirent ent;

	assert(file);

	create_log_dir(CONFIG_DATA_FS_DIR);
	
	if (data_state == DATA_FS_NOT_INITIALIZED) {
		/* Search for the last used log number. */
		struct fs_dir_t dir;
		int file_num = 0;
		
		fs_dir_t_init(&dir);
		curr_file_num = 0;
		int max = 0, min = MAX_FILE_NUMERAL;

		rc = fs_opendir(&dir, CONFIG_DATA_FS_DIR);
		
		while (rc >= 0) {
			rc = fs_readdir(&dir, &ent);
			if ((rc < 0) || (ent.name[0] == 0)) {
				
				break;
			}
			file_num = get_log_file_id(&ent);
			if (file_num >= 0) {
				if (file_num > max) {
					max = file_num;
				}

				if (file_num < min) {
					min = file_num;
				}
				++file_ctr;
			}
		}
		oldest = min;
		if ((file_ctr > 1) &&
		    ((max - min) >
		     2 * CONFIG_DATA_FS_FILES_LIMIT)) {
			/* oldest log is in the range around the min */
			newest = min;
			oldest = max;
			(void)fs_closedir(&dir);
			rc = fs_opendir(&dir, CONFIG_DATA_FS_DIR);
			while (rc == 0) {
				rc = fs_readdir(&dir, &ent);
				if ((rc < 0) || (ent.name[0] == 0)) {
					break;
				}
				file_num = get_log_file_id(&ent);
				if (file_num < min + CONFIG_DATA_FS_FILES_LIMIT) {
					if (newest < file_num) {
						newest = file_num;
					}
				}
				if (file_num > max - CONFIG_DATA_FS_FILES_LIMIT) {
					if (oldest > file_num) {
						oldest = file_num;
					}
				}
			}
		} else {
			newest = max;
			oldest = min;
		}
		(void)fs_closedir(&dir);
		if (rc < 0) {
			goto out;
		}
		curr_file_num = newest;

		if (file_ctr >= 1) {
			curr_file_num++;
			if (curr_file_num > MAX_FILE_NUMERAL) {
				
				curr_file_num = 0;
			}
		}
		data_state = DATA_FS_OK;
	} else {
		fs_close(file);
		
		curr_file_num = newest;
		curr_file_num++;
		if (curr_file_num > MAX_FILE_NUMERAL) {
			curr_file_num = 0;
		}
	}
	rc = fs_statvfs(CONFIG_DATA_FS_DIR, &stat);

	/* Check if there is enough space to write file or max files number
	 * is not exceeded.
	 */
	while ((file_ctr >= CONFIG_DATA_FS_FILES_LIMIT) ||
	       ((stat.f_bfree * stat.f_frsize) <=
		CONFIG_DATA_FS_FILE_SIZE)) {
		if (IS_ENABLED(CONFIG_DATA_FS_OVERWRITE)) {
			rc = del_oldest_log();
			if (rc < 0) {
				goto out;
			}
			rc = fs_statvfs(CONFIG_DATA_FS_DIR,
					&stat);
			if (rc < 0) {
				goto out;
			}
		} else {
			return -ENOSPC;
		}
	}
	memset(file_name, 0, sizeof(file_name));
	snprintf(file_name, sizeof(file_name), "%s/%s%04d",
		CONFIG_DATA_FS_DIR,
		CONFIG_DATA_FS_FILE_PREFIX, curr_file_num);

	rc = fs_open(file, file_name, FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		goto out;
	}
	++file_ctr;
	newest = curr_file_num;

out:
	return rc;
}

static int del_oldest_log(void)
{
	int rc;
	memset(file_name, 0, sizeof(file_name));

	while (true) {
		snprintf(file_name, sizeof(file_name), "%s/%s%04d",
			 CONFIG_DATA_FS_DIR,
			 CONFIG_DATA_FS_FILE_PREFIX, oldest);
		rc = fs_unlink(file_name);
		
		if ((rc == 0) || (rc == -ENOENT)) {
			oldest++;
			if (oldest > MAX_FILE_NUMERAL) {
				oldest = 0;
			}

			if (rc == 0) {
				--file_ctr;
				break;
			}
		} else {
			break;
		}
	}

	return rc;
}

static void etc_data_fs_write(const uint8_t* msg, size_t msg_len) {
	int rc = write_log_to_file(msg, msg_len);
	if (rc != msg_len) {
		LOG_ERR("Failed to write data");
	} else {
		LOG_HEXDUMP_DBG(msg, msg_len, "LOG_DONE");
	}
}

static void etc_data_fs_write_file(const uint8_t* msg, size_t msg_len) {
	struct fs_file_t file;
	int rc;

	create_log_dir(CONFIG_DATA_FS_DIR);

	fs_file_t_init(&file);
	memset(file_name, 0, sizeof(file_name));
	snprintf(file_name, sizeof(file_name), "%s/%s%d",
		CONFIG_DATA_FS_DIR,
		CONFIG_DATA_FS_FILE_PREFIX, k_uptime_get_32());
	rc = fs_open(&file, file_name, FS_O_CREATE | FS_O_RDWR);
	if (rc < 0) {
		LOG_ERR("FAIL: open file: %d", rc);
		return;
	}

	rc = fs_truncate(&file, 0);
	if (rc) {
		LOG_ERR("Failed to truncate %s (%d)", file_name, rc);
		return;
	}
	rc = fs_write(&file, msg, msg_len);
	if (rc < 0) {
		LOG_ERR("FAIL: write file: %d", rc);
		return;
	}
	rc = fs_close(&file);
	if (rc < 0) {
		LOG_ERR("FAIL: close file: %d", rc);
		return;
	}
}

void etc_data_fs_notify_data(uint8_t* data, uint8_t len) {
	etc_data_fs_write_file(data, len);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_data_fs_clean(const struct shell *shell, size_t argc, char **argv)
{
	return 0;
}

static int lsdir(const struct shell *shell, const char *path)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;

	fs_dir_t_init(&dirp);

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res) {
		shell_print(shell, "Error opening dir %s [%d]", path, res);
		return res;
	}

	shell_print(shell, "\nListing dir %s ...", path);
	for (;;) {
		/* Verify fs_readdir() */
		res = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0) {
			if (res < 0) {
				shell_print(shell, "Error reading dir [%d]", res);
			}
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			shell_print(shell, "[DIR ] %s", entry.name);
		} else {
			shell_print(shell, "[FILE] %s (size = %zu)",
				   entry.name, entry.size);
		}
	}

	/* Verify fs_closedir() */
	fs_closedir(&dirp);

	return res;
}

static int cmd_data_fs_list(const struct shell *shell, size_t argc, char **argv)
{
	lsdir(shell, CONFIG_DATA_FS_DIR);
	return 0;
}

/* Creating subcommands (level 1 command) array for command "demo". */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_etc_log,
	SHELL_CMD(clean,   NULL, "Remove all logs", cmd_data_fs_clean),
	SHELL_CMD(list,   NULL, "List all logs", cmd_data_fs_list),
	SHELL_SUBCMD_SET_END
);
/* Creating root (level 0) command "demo" */
SHELL_CMD_REGISTER(etc_log, &sub_etc_log, "ETC Data Record Log Commands", NULL);
#endif