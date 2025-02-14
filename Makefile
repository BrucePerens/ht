# Built for various platforms.
ESP_IDF?=~/esp/esp-idf

native:
	$(MAKE) --no-print-directory -f platform/Makefile.native

k4vp:
	mkdir -p build.k4vp
	platform/k4vp_2/run_idf.sh platform/k4vp_2 -B ../../build.k4vp build

k4vp.flash:
	mkdir -p build.k4vp
	platform/k4vp_2/run_idf.sh platform/k4vp_2 -B ../../build.k4vp flash

clean:
	rm -f -r build.*
