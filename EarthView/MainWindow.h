#pragma once
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>

class MapWidget;
class MapViewWindow;
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
    /// Open (or raise) the osgEarth 3D view on what the 2D map is looking at. Only built
    /// when earthview3d is (EARTHVIEW_HAVE_3D); the action is hidden otherwise (roadmap E9).
    void open3DView();

private:
    MapWidget* m_mapWidget;
    MapViewWindow* m_secondaryWindow;
    QPointer<QMainWindow> m_window3D;   // the 3D view's window, when one is open
    QLabel* m_coordLabel;
    QLabel* m_zoomLabel;
};

#endif // MAINWINDOW_H
