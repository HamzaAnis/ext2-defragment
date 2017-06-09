COMPILER=gcc;
mount: ; dd if=/dev/zero of=image.img bs=1024 count=128; mke2fs -N 32 image.img; mkdir mnt; sudo mount -o loop image.img mnt;
all: ; gcc 	-o defragext2 main.c;
unmount: ; sudo umount mnt; rm defragext2;
delete: ; sudo umount mnt; sudo rmdir mnt; rm image.img;