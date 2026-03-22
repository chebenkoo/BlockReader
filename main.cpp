#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDebug>

// CGAL includes
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef K::Point_3 Point;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Creates a simple CGAL point to test linking
    Point p(1.0, 2.0, 3.0);
    qDebug() << "CGAL Integration Test: Point created at (" << p.x() << "," << p.y() << "," << p.z() << ")";

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("MiningSchedule", "Main");

    return app.exec();
}
