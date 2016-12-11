#!/bin/sh

make clean
make
rm -rf super1 super2 child1 child2
mkdir super1 super2 child1 child2 
cp ./super super1
cp ./super super2
cp ./child child1
cp ./child child2

cd super1
gnome-terminal --tab -e "/bin/bash -c './super -p 11111; exec /bin/bash -i'"
cd ..

cd super2
gnome-terminal --tab -e "/bin/bash -c './super -p 11112 --s_ip 127.0.0.1 --s_port 11111; exec /bin/bash -i'"
cd ..

sleep 1s

cd child1
rm -rf download data
mkdir download
mkdir data
cp ../tmp/1/* data
gnome-terminal --tab -e "/bin/bash -c './child -p 22221 --s_ip 127.0.0.1 --s_port 11111; exec /bin/bash -i'"
cd ..

sleep 1s

cd child2
rm -rf download data
mkdir download
mkdir data
cp ../tmp/2/* data
gnome-terminal --tab -e "/bin/bash -c './child -p 22222 --s_ip 127.0.0.1 --s_port 11112; exec /bin/bash -i'"
cd ..


