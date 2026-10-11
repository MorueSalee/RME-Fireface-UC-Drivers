CFLAGS = -Wall -Wextra -Werror -O2 -I shim -I ../sound-usb

test_rme_uc: test_rme_uc.c ../sound-usb/rme_uc.c ../sound-usb/rme_uc.h
	$(CC) $(CFLAGS) -o $@ test_rme_uc.c ../sound-usb/rme_uc.c

check: test_rme_uc
	./test_rme_uc

clean:
	rm -f test_rme_uc

.PHONY: check clean
