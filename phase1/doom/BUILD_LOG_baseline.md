Baseline command: make clean all CC=gcc CSTD=c11 CFLAGS="-std=c11 -Wall -Wextra -O2 -g"
rm -rf build
mkdir -p build/baseline
gcc -DNORMALUNIX -DLINUX -std=c11 -Wall -Wextra -O2 -g -c vendor/idoom/linuxdoom-1.10/doomdef.c -o build/baseline/doomdef.o
vendor/idoom/linuxdoom-1.10/doomdef.c:26:1: warning: unused variable 'rcsid' [-Wunused-const-variable]
   26 | rcsid[] = "$Id: m_bbox.c,v 1.1 1997/02/03 22:45:10 b1 Exp $";
      | ^~~~~
1 warning generated.
gcc -DNORMALUNIX -DLINUX -std=c11 -Wall -Wextra -O2 -g -c vendor/idoom/linuxdoom-1.10/doomstat.c -o build/baseline/doomstat.o
In file included from vendor/idoom/linuxdoom-1.10/doomstat.c:31:
In file included from vendor/idoom/linuxdoom-1.10/doomstat.h:33:
In file included from vendor/idoom/linuxdoom-1.10/doomdata.h:28:
vendor/idoom/linuxdoom-1.10/doomtype.h:42:10: fatal error: 'values.h' file not found
   42 | #include <values.h>
      |          ^~~~~~~~~~
1 error generated.
make: *** [build/baseline/doomstat.o] Error 1

Exit status: 2
