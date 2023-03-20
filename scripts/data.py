import argparse
import logging
import os
from mcumgr import McuMgrExecutor
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

DATA_MTU=320

def data_check_response(output, msg_in_err):
    status = int(output.splitlines()[0].split("=")[-1])
    if status != 0:
        logger.error( f"{msg_in_err}")
        exit(0)
    
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    # Add the argument for connection string (COM port)
    parser.add_argument("--conn", help="Serial connection for Monitor Devices")
    parser.add_argument("--cmd", help="Command to execute over MCUMGR")
    
    args = parser.parse_args()
    conn_string = args.conn
    if conn_string is None:
        logger.error("No connection string")
    else:
        mcumgr = McuMgrExecutor(conn_string=conn_string, conn_type="serial")
        if args.cmd == "retrieve" :
            ret_status, output = mcumgr.shell_command("record report")
            data_check_response(output, "Failed to send command to get report of record")
            report_nack_record_line = output.splitlines()[-1]
            report_num_nack_record = int(report_nack_record_line.split()[-1])
            logger.info(f"Number of NACK in current device {report_num_nack_record}")
            
            ret_status, output = mcumgr.shell_command("record nack_list")
            report_nack_id_lines = output.splitlines()
            nack_list = []
            data_check_response(output, "Failed to send command to list all NACK")
            del report_nack_id_lines[0] # Remove line status
            report_nack_id_lines = [x for x in report_nack_id_lines if x != ''] # Remove obsoleted empty id
            if len(report_nack_id_lines) == 0:
                logger.warning("We don't have record ID for NACK")
                exit(0)
                
            for nack_id in report_nack_id_lines:
                ret_status, output = mcumgr.shell_command(f"record nack_id {nack_id}")
                nack_data_lines = output.splitlines()
                data_check_response(output, f"Failed to get data at ID {nack_id}")
                
                del nack_data_lines[0] # Remove line status
                nack_data_lines = [x for x in nack_data_lines if x != '']
                if len(nack_data_lines) != 1:
                    logger.info(len(nack_data_lines))
                    logger.error( f"Invalid data for record")
                    exit(0)

                nack_data = nack_data_lines[0]
                ret_status, output = mcumgr.shell_command(f"record parser {nack_data}")
                nack_result_lines = output.splitlines()
                data_check_response(output, f"Failed to convert data at ID {nack_id}")
                del nack_result_lines[0] # Remove line status
                nack_result_lines = [x for x in nack_result_lines if x != '']
                logger.info( f"Record {nack_id} : {nack_result_lines}") 
                
        elif args.cmd == "clean" :
            ret_status, output = mcumgr.shell_command(f"record clean")
            status = int(output.splitlines()[0].split("=")[-1])
            if status != 0:
                logger.error(f"Failed to clean up the record")
                exit(0)
                
        elif args.cmd == "reset":
            mcumgr.reset_command()
            logger.info("Reset the device successfully")
            
        else:
            logger.warning( f"No support command {args.cmd}")
