# Errata for *Hands-On Booting*

In **chapter 6 dracut - below figure 6-10** [Technical accuracy]:
 
fsck depends on the fifth parameter of /etc/fstab. If it is 1, then fsck will be performed at the time of boot
fstab entry (UUID=eea3d947-0618-4d8c-b083-87daf15b2679 /boot  ext4    defaults  1 2) the 5th field is 1 , this field was used by dump command , the last (6th) field is for fsck.

***

On **page xx** [Summary of error]:
 
Details of error here. Highlight key pieces in **bold**.

***
