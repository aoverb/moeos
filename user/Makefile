SUBDIRS := $(sort $(dir $(wildcard */Makefile)))

all:
	@for dir in $(SUBDIRS); do \
		echo "=== Building $$dir ==="; \
		$(MAKE) -C $$dir all || exit 1; \
	done

install-bin:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir install-bin || exit 1; \
	done

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done