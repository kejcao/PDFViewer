pdf: main.cpp
	g++ -lsfml-graphics -lsfml-window -lsfml-system -lmupdf main.cpp -o pdf
