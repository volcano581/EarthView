#pragma once
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class MapWidget;
class QLabel;

/**
 * @brief MainWindow is the main application window
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setupUI();
    void createStatusBar();

private:
    MapWidget* m_mapWidget;
    QLabel* m_coordLabel;
    QLabel* m_zoomLabel;
};

#endif // MAINWINDOW_H