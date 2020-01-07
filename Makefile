SUBDIRS = src java
MBOT_SUBDIRS = src
MAKEFLAGS += --no-print-directory

all:
	@for dir in $(SUBDIRS); do \
	echo "[$$dir]"; $(MAKE) -C $$dir all || exit 2; done

mbot-only:
	@for dir in $(MBOT_SUBDIRS); do \
	echo "[$$dir]"; $(MAKE) -C $$dir mbot-only || exit 2; done

laptop-only:
	@for dir in $(MBOT_SUBDIRS); do \
	echo "[$$dir]"; $(MAKE) -C $$dir laptop-only || exit 2; done
	echo "[java]"; $(MAKE) -C java all || exit 2;

clean:
	@for dir in $(SUBDIRS); do \
	echo "clean [$$dir]"; $(MAKE) -C $$dir clean || exit 2; done
	@rm -f *~
