# Built for various platforms.
ESP_IDF?=~/esp/esp-idf

native:
	$(MAKE) --no-print-directory -f platform/Makefile.native

k6vp:
	mkdir -p build.k6vp
	aux/run_idf.sh platform/k6vp_2 -B ../../build.k6vp build

k6vp.flash:
	mkdir -p build.k6vp
	aux/run_idf.sh platform/k6vp_2 -B ../../build.k6vp flash

clean:
	rm -f -r build.*
