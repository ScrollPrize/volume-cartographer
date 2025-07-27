#include <qapplication.h>

#include "vc/Version.hpp"
#include "CWindow.hpp"

#include <thread>
#include <opencv2/core.hpp>



auto main(int argc, char* argv[]) -> int
{
    cv::setNumThreads(std::thread::hardware_concurrency());
    
    QApplication app(argc, argv);
    QApplication::setOrganizationName("EduceLab");
    QApplication::setApplicationName("VC3D");
    QApplication::setWindowIcon(QIcon(":/images/logo.png"));
    QApplication::setApplicationVersion(QString::fromStdString("1.2.3"));

    CWindow aWin;
    aWin.show();
    return QApplication::exec();
}
