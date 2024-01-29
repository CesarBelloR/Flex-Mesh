#! /usr/bin/env python3
#
# Copyright 2017-2020 Linaro Limited
# Copyright 2019-2023 Arm Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright 2023 EXACT Technology
#
import click
import struct
import getpass
import imgtool.keys as keys
import imgtool.image as image
import sys
import os.path
import base64
from imgtool import image, imgtool_version
import imgtool.dumpinfo as imginfo

MIN_PYTHON_VERSION = (3, 6)
if sys.version_info < MIN_PYTHON_VERSION:
    sys.exit("Python %s.%s or newer is required by imgtool."
             % MIN_PYTHON_VERSION)

valid_hash_encodings = ['hex', 'raw']

_LINE_LENGTH = 60

def get_id_from_key_hash(key_hash):
    return key_hash[:4]

def extract_public_key_hash(tlv_list):    
    for tlv in tlv_list:
        if tlv["type"] == image.TLV_VALUES['KEYHASH']:
            return tlv["data"]
    
    return None

'''Most of this function is taken from imgtool.dumpinfo.dump_imginfo'''
@click.argument('imgfile')
@click.option('-v', '--verbose', default=False, is_flag=True,
              help='Print additional information')
@click.command(help='Print header, TLV area and trailer information '
                    'of a signed image')
def pubkeyid(imgfile, verbose=False):
    '''Parse a signed image binary and print/save the available information.'''
    try:
        with open(imgfile, "rb") as f:
            b = f.read()
    except FileNotFoundError:
        raise click.UsageError("Image file not found ({})".format(imgfile))

    # Parsing the image header
    _header = struct.unpack('IIHHIIBBHI', b[:28])
    # Image version consists of the last 4 item ('BBHI')
    _version = _header[-4:]
    header = {}
    for i, key in enumerate(imginfo.HEADER_ITEMS):
        if key == "version":
            header[key] = "{}.{}.{}+{}".format(*_version)
        else:
            header[key] = _header[i]

    # Parsing the TLV area
    tlv_area = {"tlv_hdr_prot": {},
                "tlvs_prot":    [],
                "tlv_hdr":      {},
                "tlvs":         []}
    tlv_off = header["hdr_size"] + header["img_size"]
    protected_tlv_size = header["protected_tlv_size"]

    if protected_tlv_size != 0:
        _tlv_prot_head = struct.unpack(
                            'HH',
                            b[tlv_off:(tlv_off + image.TLV_INFO_SIZE)])
        tlv_area["tlv_hdr_prot"]["magic"] = _tlv_prot_head[0]
        tlv_area["tlv_hdr_prot"]["tlv_tot"] = _tlv_prot_head[1]
        tlv_end = tlv_off + tlv_area["tlv_hdr_prot"]["tlv_tot"]
        tlv_off += image.TLV_INFO_SIZE

        # Iterating through the protected TLV area
        while tlv_off < tlv_end:
            tlv_type, tlv_len = struct.unpack(
                                    'HH',
                                    b[tlv_off:(tlv_off + image.TLV_INFO_SIZE)])
            tlv_off += image.TLV_INFO_SIZE
            tlv_data = b[tlv_off:(tlv_off + tlv_len)]
            tlv_area["tlvs_prot"].append(
                {"type": tlv_type, "len": tlv_len, "data": tlv_data})
            tlv_off += tlv_len

    _tlv_head = struct.unpack('HH', b[tlv_off:(tlv_off + image.TLV_INFO_SIZE)])
    tlv_area["tlv_hdr"]["magic"] = _tlv_head[0]
    tlv_area["tlv_hdr"]["tlv_tot"] = _tlv_head[1]

    tlv_end = tlv_off + tlv_area["tlv_hdr"]["tlv_tot"]
    tlv_off += image.TLV_INFO_SIZE

    # Iterating through the TLV area
    while tlv_off < tlv_end:
        tlv_type, tlv_len = struct.unpack(
                                'HH',
                                b[tlv_off:(tlv_off + image.TLV_INFO_SIZE)])
        tlv_off += image.TLV_INFO_SIZE
        tlv_data = b[tlv_off:(tlv_off + tlv_len)]
        tlv_area["tlvs"].append(
            {"type": tlv_type, "len": tlv_len, "data": tlv_data})
        tlv_off += tlv_len

    _img_pad_size = len(b) - tlv_end
        
    if _img_pad_size:
        trailer = {}
        # Parsing the image trailer
        trailer_off = -imginfo.BOOT_MAGIC_SIZE
        trailer_magic = b[trailer_off:]
        trailer["magic"] = trailer_magic
        max_align = None
        if trailer_magic == imginfo.BOOT_MAGIC:
            # The maximum supported write alignment is the default 8 Bytes
            max_align = 8
        elif trailer_magic[-len(imginfo.BOOT_MAGIC_2):] == imginfo.BOOT_MAGIC_2:
            # The alignment value is encoded in the magic field
            max_align = int(trailer_magic[:2], 0)
        else:
            # Invalid magic: the rest of the image trailer cannot be processed.
            print("Warning: the trailer magic value is invalid!")

        if max_align is not None:
            if max_align > imginfo.BOOT_MAGIC_SIZE:
                trailer_off -= max_align - imginfo.BOOT_MAGIC_SIZE
            # Parsing rest of the trailer fields
            trailer_off -= max_align
            image_ok = b[trailer_off]
            trailer["image_ok"] = image_ok

            trailer_off -= max_align
            copy_done = b[trailer_off]
            trailer["copy_done"] = copy_done

            trailer_off -= max_align
            swap_info = b[trailer_off]
            trailer["swap_info"] = swap_info

            trailer_off -= max_align
            swap_size = int.from_bytes(b[trailer_off:(trailer_off + 4)],
                                       "little")
            trailer["swap_size"] = swap_size

            # Encryption key 0/1
            key_field_len = None
            if ((header["flags"] & image.IMAGE_F["ENCRYPTED_AES128"]) or
               (header["flags"] & image.IMAGE_F["ENCRYPTED_AES256"])):
                # The image is encrypted
                #    Estimated value of key_field_len is correct if
                #    BOOT_SWAP_SAVE_ENCTLV is unset
                key_field_len = image.align_up(16, max_align) * 2

###############################################################################
    key_hash = extract_public_key_hash(tlv_area["tlvs"])
    if verbose:
        print(key_hash.hex())
    pubkey_id = get_id_from_key_hash(key_hash)
    print(pubkey_id.hex())


@click.option('-e', '--encoding', metavar='encoding',
              type=click.Choice(valid_hash_encodings),
              help='Valid encodings: {}. '
                   'Default value is {}.'
                   .format(', '.join(valid_hash_encodings),
                           valid_hash_encodings[0]))
@click.option('-k', '--key', metavar='filename', required=True)
@click.option('-o', '--output', metavar='output', required=False,
              help='Specify the output file\'s name. \
                    The stdout is used if it is not provided.')
@click.command(help='Dump the SHA256 hash of the public key')
def getpubhash(key, output, encoding):
    if not encoding:
        encoding = valid_hash_encodings[0]
    key = load_key(key)

    if not output:
        output = sys.stdout
    if key is None:
        print("Invalid passphrase")
    elif encoding == 'lang-c':
        key.emit_c_public_hash(file=output)
    elif encoding == 'raw':
        key.emit_raw_public_hash(file=output)
    else:
        raise click.UsageError()

@click.group()
def etc_imgtool():
    pass


etc_imgtool.add_command(pubkeyid)


if __name__ == '__main__':
    etc_imgtool()
