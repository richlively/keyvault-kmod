
/* Author:  Keith Shomper
 * Date:    8 Nov 2016
 * Purpose: Rudimentary testing for the key vault implementation
 *          that is embedded in a kernel module.  See main.c for
 *          for a test program that tests a user space version of
 *          the key_vault (Key-Vault-V3).
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

int main () {
	char buf[2];
	int  fd;

   if ((fd = open ("/dev/kv_mod", O_WRONLY)) == -1) {
     perror("opening file");
     return -1;
   }
	
	strncpy(buf, "", 1);
	write(fd, buf, 1);

   close(fd);

   return 0;
}
