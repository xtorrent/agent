TOP=.
PREFIX=/usr/local/libtorrent
BOOST=/usr/local/boost
LIBTORRENT_PKG=libtorrent_0_16_5.tar.gz
LIBTORRENT_URL=https://github.com/arvidn/libtorrent/archive/libtorrent_0_16_5.tar.gz
LIBTORRENT_MD5SUM=2e34ec5c608fab507106557974a2ccd1
LIBTORRENT_PATCH=libtorrent-0.16.5.patch

all: download
	tar -zxf $(LIBTORRENT_PKG)
	
	cd $$(tar tzf $(LIBTORRENT_PKG) | head -1) && patch -p1 < ../$(LIBTORRENT_PATCH) \
		&& autoreconf --verbose --install --force \
		&& ./configure \
			--with-boost=$(BOOST) \
			--enable-debug=no \
			--disable-dht \
			--disable-geoip \
			--disable-deprecated-functions \
			--enable-examples=no \
			--disable-encryption \
			--enable-tests=no \
			--enable-static=yes \
			--enable-shared=no \
			--prefix=$(PREFIX) \
		&& make \
		&& make install

download:
	echo $(LIBTORRENT_MD5SUM) $(LIBTORRENT_PKG) | md5sum --check || curl -fsSL $(LIBTORRENT_URL) > $(LIBTORRENT_PKG)
	echo $(LIBTORRENT_MD5SUM) $(LIBTORRENT_PKG) | md5sum --check
	


clean:
	rm -fr $$(tar tzf $(LIBTORRENT_PKG) | head -1)
	rm -fr /usr/local/libtorrent
