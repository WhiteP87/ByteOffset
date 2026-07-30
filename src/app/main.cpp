#include <QApplication>
#include <QFontDatabase>
#include <QFile>
#include "HexEditor.h"
#include "MainWindow.h"

#include <iostream>
#include <vector>

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	MainWindow mw;
	mw.show();
	return app.exec();
}