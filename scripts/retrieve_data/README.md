# Prerequisites
* **Python 3.9 and above** : Download and install Python from <a href="https://www.python.org/downloads/">here</a>
* Install required python packages with `pip install -r requirements.txt`

# Usage
* Ensure Monitor 2.0 is connected to your computer over USB and turned on
* Download data saved in the device's memory with:
    ```
    python record.py --conn <serial_port> --cmd retrieve --file="<filename>"
    ```
    * <serial_port>: Device's serial port: COMx for Windows, /dev/ttyACMx for Linux
    * <filename>: A file name of your choice that the data will be saved to in csv format
