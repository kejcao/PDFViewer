pdf: imgui imgui-sfml SFML main.cpp backends/*
	g++ -std=c++20 -ISFML/include -Iimgui -Iimgui-sfml \
		-lmupdf -larchive -lGL -lX11 -lXrandr -ludev -lXcursor -lXi \
		main.cpp \
		imgui/imgui.cpp \
		imgui/imgui_draw.cpp \
		imgui/imgui_widgets.cpp \
		imgui/imgui_tables.cpp \
		imgui-sfml/imgui-SFML.cpp \
		SFML/lib/libsfml-graphics-s.a \
		SFML/lib/libsfml-window-s.a \
		SFML/lib/libsfml-system-s.a \
		-o pdf

imgui:
	git clone --depth=1 https://github.com/ocornut/imgui
imgui-sfml:
	git clone --depth=1 https://github.com/SFML/imgui-sfml
	cat imgui-sfml/imconfig-SFML.h >>imgui/imconfig.h
SFML:
	git clone --depth=1 --branch 3.0.x https://github.com/SFML/SFML/
	cd SFML && cmake . && make
