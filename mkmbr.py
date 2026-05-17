import sys
import struct
import os

def create_mbr_partition(img_path, part_num, size_mb):
    """
    Создаёт MBR раздел в образе диска
    part_num: 1-4
    size_mb: размер в мегабайтах
    """

    if part_num < 1 or part_num > 4:
        print("Error: partition number must be 1-4")
        return False

    sector_size = 512
    mb_to_sectors = 2048
    size_sectors = size_mb * mb_to_sectors

    if not os.path.exists(img_path):
        print(f"Error: {img_path} not found")
        return False

    with open(img_path, 'r+b') as f:

        f.seek(0)
        mbr = bytearray(f.read(512))

        if len(mbr) < 512:
            mbr.extend(b'\x00' * (512 - len(mbr)))

        signature = struct.unpack('<H', mbr[510:512])[0]

        start_sector = 2048
        part_offset = 446 + (part_num - 1) * 16

        if mbr[part_offset + 4] != 0:
            print(f"Error: partition {part_num} already exists")
            return False

        max_end = 2048
        for i in range(4):
            offset = 446 + i * 16
            if mbr[offset + 4] != 0:
                lba_start = struct.unpack('<I', mbr[offset+8:offset+12])[0]
                sector_count = struct.unpack('<I', mbr[offset+12:offset+16])[0]
                end = lba_start + sector_count
                if end > max_end:
                    max_end = end

                    print(f"Found partition {i+1}: start={lba_start}, size={sector_count}")

        start_sector = max_end
        print(f"New partition will start at sector {start_sector}")

        file_size = os.path.getsize(img_path)
        total_sectors = file_size // sector_size
        if start_sector + size_sectors > total_sectors:
            print(f"Error: Not enough space. Need {size_sectors} sectors, have {total_sectors - start_sector}")
            return False

        mbr[part_offset] = 0x00

        mbr[part_offset+1:part_offset+4] = b'\x00\x00\x00'

        mbr[part_offset+4] = 0x83

        mbr[part_offset+5:part_offset+8] = b'\x00\x00\x00'

        struct.pack_into('<I', mbr, part_offset+8, start_sector)

        struct.pack_into('<I', mbr, part_offset+12, size_sectors)

        mbr[510] = 0x55
        mbr[511] = 0xAA

        f.seek(0)
        f.write(mbr)

        print(f"Partition {part_num} created: start={start_sector}, size={size_mb} MB ({size_sectors} sectors)")
        return True

def main():
    if len(sys.argv) != 4:
        print("Usage: mkmbr.py <image_file> <partition_number> <size_mb>")
        print("Example: mkmbr.py disk.img 1 100")
        sys.exit(1)

    img_path = sys.argv[1]
    part_num = int(sys.argv[2])
    size_mb = int(sys.argv[3])

    if create_mbr_partition(img_path, part_num, size_mb):
        print("Done!")
    else:
        print("Failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()