sudo umount /mnt/bbssd
sudo rmmod himfs
sudo insmod himfs.ko
sudo mount -t himfs /dev/nvme0n1 /mnt/bbssd
cat /proc/mounts | grep himfs
sudo bash -c "echo 0 > /proc/sys/kernel/randomize_va_space"