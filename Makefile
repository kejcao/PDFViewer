CC = g++

LIBS = -lmupdf -lzip -lGL -lX11 -lXrandr -ludev -lXcursor -lXi -ldjvulibre
CFLAGS = -O3 -march=native -std=c++20 -ISFML/include -fopenmp -Iimgui -Iimgui-sfml

pdf: imgui imgui-sfml SFML main.cpp imgui.a backends/*
	$(CC) $(CFLAGS) $(LIBS) \
		main.cpp \
		imgui.a \
		SFML/lib/libsfml-graphics-s.a \
		SFML/lib/libsfml-window-s.a \
		SFML/lib/libsfml-system-s.a \
		-o pdf

imgui.a:
	$(CC) $(CFLAGS) $(LIBS) -c imgui/imgui.cpp           -o 1.o
	$(CC) $(CFLAGS) $(LIBS) -c imgui/imgui_draw.cpp      -o 2.o
	$(CC) $(CFLAGS) $(LIBS) -c imgui/imgui_widgets.cpp   -o 3.o
	$(CC) $(CFLAGS) $(LIBS) -c imgui/imgui_tables.cpp    -o 4.o
	$(CC) $(CFLAGS) $(LIBS) -c imgui-sfml/imgui-SFML.cpp -o 5.o
	ar rcs imgui.a 1.o 2.o 3.o 4.o 5.o

imgui:
	git clone --depth=1 https://github.com/ocornut/imgui
imgui-sfml:
	git clone --depth=1 https://github.com/SFML/imgui-sfml
	cat imgui-sfml/imconfig-SFML.h >>imgui/imconfig.h
SFML:
	git clone --depth=1 --branch 3.0.x https://github.com/SFML/SFML/
	cd SFML && cmake . && make

clean:
	rm -rf imgui.a *.o pdf
