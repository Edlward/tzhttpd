 g++ -std=c++0x -Wall  main.cpp -I../ -I../../xtra_rhel6.x/include -L../build/ -L../../xtra_rhel6.x/libs/debug/boost/ -ltzhttpd -ltzhttpd -lboost_system -lboost_thread -lboost_date_time -lboost_regex -lpthread -lrt -lconfig++ -o httpsrv