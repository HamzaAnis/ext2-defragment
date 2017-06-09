all: ; mkdir mnt; sudo mount -o loop image.img mnt; gcc -o defragext2 main.c; ./defragext2 image.img;
clear: ; sudo umount mnt; rm defragext2;