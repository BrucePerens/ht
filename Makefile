# Built for various platforms.
ESP_IDF?=~/esp/esp-idf

native:
	$(MAKE) --no-print-directory -f platform/Makefile.native

k6vp:
	mkdir -p build.k6vp
	(if test -f "$(IDF_PATH)"; then \
          . $(ESP_IDF)/export.sh; \
         fi; \
         cd platform/k6vp_2; \
         idf.py -B ../../build.k6vp build)

k6vp.flash:
	mkdir -p build.k6vp
	(if test -f "$(IDF_PATH)"; then \
          . $(ESP_IDF)/export.sh; \
         fi; \
         cd platform/k6vp_2; \
         idf.py -B ../../build.k6vp flash)

clean:
	rm -f -r build.*
