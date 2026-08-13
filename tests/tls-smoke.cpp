#include <QCoreApplication>
#include <QDir>
#include <QSslSocket>

#include <iostream>

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  if (argc != 2) {
    std::cerr << "Usage: voxlocal-tls-smoke <plugin-runtime-directory>\n";
    return 2;
  }

  const QString pluginRoot = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1]));
  QCoreApplication::setLibraryPaths({pluginRoot});
  const auto backends = QSslSocket::availableBackends();
  std::cout << "Qt TLS backends:";
  for (const auto &backend : backends)
    std::cout << ' ' << backend.toStdString();
  std::cout << '\n';

  if (!backends.contains(QStringLiteral("schannel")) || !QSslSocket::supportsSsl()) {
    std::cerr << "The staged plugin has no functional Windows Schannel TLS backend.\n";
    return 1;
  }
  std::cout << "Windows Schannel TLS backend loaded successfully.\n";
  return 0;
}
