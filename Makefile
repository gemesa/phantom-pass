SUBDIRS := \
src/0-hello-world \
src/1-string-xor-encryption \
src/2-string-base64-encoding \
src/3-string-xor-encryption

.PHONY: all clean run format format-check $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

run:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir run; \
	done

format:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir format; \
	done

format-check:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir format-check; \
	done
