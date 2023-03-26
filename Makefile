all: hw3.cpp
	g++ *.c hw3.cpp -o hw3 -g
clean:
	rm example.img & cp ~/example.img .
file:
	fusermount -u droot & fusefat -o rw+ -o umask=770 example.img droot
check:
	fsck.vfat -vn example.img

