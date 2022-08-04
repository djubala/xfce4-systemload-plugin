#!/bin/bash

rm -rf ../xfce4-systemload-plugin-install
mkdir ../xfce4-systemload-plugin-install
installdir=$(realpath ../xfce4-systemload-plugin-install)

./autogen.sh --prefix="$installdir"
make
make install

cp -av systemload2.desktop /usr/share/xfce4/panel/plugins/

cd "$installdir"

cp -av lib/xfce4/panel/plugins/libsystemload.so /usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libsystemload2.so

cd share/locale
for i in */*/* ; do
  mv -v $i $(echo $i | sed 's/systemload/systemload2/') ;
done
rsync -av ./ /usr/share/locale/

xfce4-panel -r
