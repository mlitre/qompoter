#include <QCoreApplication>
#include <QTextCodec>
#include <QProcess>
#include <QDir>
#include <QDebug>
#include <QCommandLineParser>

#include "ConfigFileManager.h"
#include "Config.h"
#include "Query.h"
#include "DependencyInfo.h"
#include "FsLoader.h"
#include "GitLoader.h"

using namespace Qompoter;

enum CommandLineParseResult
{
    CommandLineOk,
    CommandLineError,
    CommandLineVersionRequested,
    CommandLineHelpRequested
};

CommandLineParseResult parseCommandLine(QCommandLineParser &parser, Query &query, QString &errorMessage)
{
    parser.setApplicationDescription(QCoreApplication::applicationName());
    QStringList availableActions;
    availableActions<<"install"<<"update"<<"make";
    parser.addPositionalArgument("action", QObject::tr("Action to execute: ")+availableActions.join(", "));
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();
    QCommandLineOption verboseOption(QStringList() << "V" << "verbose", QObject::tr("Increase verbosity of messages"));
    parser.addOption(verboseOption);
    QCommandLineOption globalOption(QStringList() << "g" << "global", QObject::tr("Install the dependency on the machine, instead of localy, for the current project only"));
    parser.addOption(globalOption);
    QCommandLineOption workingDirOption(QStringList() << "d" << "working-dir", QObject::tr("If specified, use the given directory as working directory"), QObject::tr("directory"));
    parser.addOption(workingDirOption);
    QCommandLineOption vendorDirOption(QStringList() << "vendor-dir", QObject::tr("If specified, use the given directory as vendor directory"), QObject::tr("directory"));
    parser.addOption(vendorDirOption);
    QCommandLineOption qompoterFileOption(QStringList() << "f" << "file", QObject::tr("If specified, use the given file as qompoter.json file"), QObject::tr("filename"));
    parser.addOption(qompoterFileOption);

    if (!parser.parse(QCoreApplication::arguments())) {
        errorMessage = parser.errorText();
        return CommandLineError;
    }
    if (parser.isSet(versionOption))
        return CommandLineVersionRequested;
    if (parser.isSet(helpOption))
        return CommandLineHelpRequested;

    if (parser.isSet(workingDirOption)) {
        query.setWorkingDir(parser.value(workingDirOption));
    }
    if (parser.isSet(vendorDirOption)) {
        query.setVendorDir(parser.value(vendorDirOption));
    }
    if (parser.isSet(qompoterFileOption)) {
        query.setQompoterFile(parser.value(qompoterFileOption));
    }
    query.setVerbose(parser.isSet(verboseOption));
    query.setGlobal(parser.isSet(globalOption));

    const QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.isEmpty()) {
        errorMessage = "Argument 'action' missing.";
        return CommandLineError;
    }
    if (positionalArguments.size() > 1) {
        errorMessage = "Several 'action' arguments specified.";
        return CommandLineError;
    }
    query.setAction(positionalArguments.at(0));
    if (!availableActions.contains(query.getAction())) {
        errorMessage = "Unknown action \""+query.getAction()+"\". Use: "+availableActions.join(", ");
        return CommandLineError;
    }
    return CommandLineOk;
}



Q_DECL_EXPORT int main(int argc, char *argv[])
{
    /***************************
     * App Configuration
     ***************************/
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Fylhan");
    QCoreApplication::setOrganizationDomain("fylhan");
    QCoreApplication::setApplicationName("Qompoter");
    QCoreApplication::setApplicationVersion("0.0.1");

    /***************************
     * Start App
     ***************************/
    QCommandLineParser parser;
    Query query;
    QString errorMessage;
    switch (parseCommandLine(parser, query, errorMessage)) {
        case CommandLineOk:
            break;
        case CommandLineError:
            fputs(qPrintable(errorMessage), stderr);
            fputs("\n\n", stderr);
            fputs(qPrintable(parser.helpText()), stderr);
            return 1;
        case CommandLineVersionRequested:
            printf("%s %s\n", qPrintable(QCoreApplication::applicationName()),
                   qPrintable(QCoreApplication::applicationVersion()));
            return 0;
        case CommandLineHelpRequested:
            parser.showHelp();
            Q_UNREACHABLE();
    }


    if (query.isVerbose()) {
        qSetMessagePattern("[%{type} | l.%{line}	| %{function}]		%{message}");
    }
    qDebug()<<"Loading qompoter repositories with package information";
    qDebug()<<"Parsing "<<query.getQompoterFile()<<"...";
    Config config = Config::fromFile(query.getWorkingDir()+query.getQompoterFile());
    if (query.isVerbose()) {
        qDebug()<<"Config:\n"<<config.toString();
    }

    bool globalResult = true;
    if ("install" == query.getAction() || "update" == query.getAction()) {
        QHash<QString, RepositoryInfo> repos;
        foreach(RepositoryInfo repo, config.repositories()) {
            repos.insert(repo.type(), repo);
        }
        repos.insert("fs", RepositoryInfo("git", "/media/Project/PlateformeVehiculeElectrique/4_workspace/"));
        repos.insert("git", RepositoryInfo("fs", "/media/Project/PlateformeVehiculeElectrique/4_workspace/"));

        FsLoader fsLoader(query);
        GitLoader gitLoader(query);
        foreach (DependencyInfo dependency, config.packages()) {
            if (!dependency.isDownloadRequired()) {
                continue;
            }
            bool found = false;
            bool updated = false;
            qDebug()<<"\t- Installing:"<<dependency.packageName()<<" ("<<dependency.version()<<")";
            foreach(RepositoryInfo repo, repos.values()) {
                if (repo.type() == "fs" && fsLoader.isAvailable(dependency, repo)) {
                    found = true;
                    updated = fsLoader.load(dependency, repo);
                    break;
                }
                else if (repo.type() == "git" && gitLoader.isAvailable(dependency, repo)) {
                    found = true;
                    updated = gitLoader.load(dependency, repo);
                    break;
                }
            }
            if (updated)
                qDebug()<<"\tdone";
            else if (!found)
                qCritical()<<"\tFAILLURE: not found package";
            else
                qCritical()<<"\tFAILLURE";
            qDebug()<<"";
            globalResult *= updated;
        }
    }
    else if ("make" == query.getAction()) {
        QFile vendorPro(query.getWorkingDir()+query.getVendorDir()+"vendor.pro");
        vendorPro.remove();
        foreach (DependencyInfo dependency, config.packages()) {
            if (!dependency.isDownloadRequired()) {
                continue;
            }
            QString packagePath(query.getWorkingDir()+query.getVendorDir()+dependency.packageName());
            QDir packageDir(packagePath);
            bool found = false;
            bool maked = false;
            qDebug()<<"\t- Compiling:"<<dependency.packageName()<<" ("<<dependency.version()<<")";
            if (packageDir.exists()) {
                QString packageBuildName("build-"+dependency.projectName());
                QDir packageBuildDir(query.getWorkingDir()+query.getVendorDir());
                if (packageBuildDir.mkpath(packageBuildName)) {
                    found = true;
                    packageBuildDir.cd(packageBuildName);
                    QProcess makeProcess;
                    makeProcess.setWorkingDirectory(packageBuildDir.path());
                    QString program;
                    QStringList arguments;
                    // Qmake
                    program = "qmake";
                    QString proFile("../"+dependency.packageName()+"/"+dependency.projectName()+".pro");
                    QFile proFd(packageBuildDir.path()+"/"+proFile);
                    if (!proFd.exists()) {
                        qDebug()<<"\tNo .pro file, try to generate one...";
                        if (!proFd.open(QIODevice::ReadWrite)) {
                            qCritical()<<"\tCan't open pro file: "<<proFd.fileName();
                        }
                        QString data;
                        data.append("PUBLIC_HEADERS += \\\n");
                        QStringList hFiles = packageDir.entryList(QStringList()<<"*.h"<<"*.hpp", QDir::Files);
                        foreach(QString file, hFiles) {
                            data.append("\t$$PWD/"+file+" \\\n");
                        }
                        data.append("\n");
                        data.append("HEADERS += \\\n");
                        data.append("\t$$PUBLIC_HEADERS \\\n");
                        data.append("\n");
                        data.append("SOURCES += \\\n");
                        QStringList cppFiles = packageDir.entryList(QStringList()<<"*.cpp", QDir::Files);
                        foreach(QString file, cppFiles) {
                            data.append("\t$$PWD/"+file+" \\\n");
                        }
                        data.append("\n");
                        data.append("\n\n");
                        data.append("LIBNAME = "+dependency.projectName()+"\n");
                        data.append("EXPORT_PATH = $$OUT_PWD/..\n");
                        data.append("EXPORT_INCLUDEPATH = $$EXPORT_PATH/include/$$LIBNAME\n");
                        data.append("EXPORT_LIBPATH = $$EXPORT_PATH/lib\n");
                        data.append("TEMPLATE = lib\n");
                        data.append("QT += core gui network script designer\n");
                        data.append("greaterThan(QT_MAJOR_VERSION, 4): QT += widgets\n");
                        data.append("CONFIG += QT\n");
                        data.append("CONFIG += staticlib\n");
                        data.append("CONFIG += debug_and_release build_all\n");
                        data.append("CONFIG(debug,debug|release) {\n");
                        data.append("\tLIBNAME = $${LIBNAME}d\n");
                        data.append("}\n");
                        data.append("TARGET = $$LIBNAME\n");
                        data.append("DESTDIR = $$EXPORT_LIBPATH\n");
                        data.append("headers.files = $$PUBLIC_HEADERS\n");
                        data.append("headers.path = $$EXPORT_INCLUDEPATH\n");
                        data.append("INSTALLS += headers\n\n");
                        proFd.write(data.toUtf8());
                        proFd.close();
                    }
                    arguments<<proFile;
                    qDebug()<<"\tRun "<<program<<" "<<arguments;
                    makeProcess.start(program, arguments);
                    maked = makeProcess.waitForFinished();
                    if (query.isVerbose()) {
                        qDebug()<<"\t"<<makeProcess.readAll();
                    }
                    if (maked) {
                        // Make
                        program = "make";
                        arguments.clear();
                        qDebug()<<"\tRun "<<program<<arguments;
                        makeProcess.start(program, arguments);
                        maked = makeProcess.waitForFinished();
                        if (query.isVerbose()) {
                            qDebug()<<"\t"<<makeProcess.readAll();
                        }
                        if (maked) {
                            // Make
                            program = "make";
                            arguments.clear();
                            arguments<<"install";
                            qDebug()<<"\tRun "<<program<<arguments;
                            makeProcess.start(program, arguments);
                            maked = makeProcess.waitForFinished();
                            if (query.isVerbose()) {
                                qDebug()<<"\t"<<makeProcess.readAll();
                            }
                        }
                    }

                    // Add stuff to vendor.pro
                    if (maked) {
                        QFile vendorPro(query.getWorkingDir()+query.getVendorDir()+"vendor.pro");
                        vendorPro.open(QIODevice::WriteOnly | QIODevice::Append);
                        QString data;
                        data.append(dependency.projectName()+" {\n");
                        data.append("\tLIBNAME = "+dependency.projectName()+"\n");
                        data.append("\tIMPORT_INCLUDEPATH = $$PWD/include/$$LIBNAME\n");
                        data.append("\tIMPORT_LIBPATH = $$PWD/lib\n");
                        data.append("\tCONFIG(debug,debug|release) {\n");
                        data.append("\t\tLIBNAME = $${LIBNAME}d\n");
                        data.append("\t}\n");
                        data.append("\tINCLUDEPATH += $$IMPORT_INCLUDEPATH\n");
                        data.append("\tLIBS += -L$$IMPORT_LIBPATH -l$${LIBNAME}\n");
                        data.append("}\n\n");
                        vendorPro.write(data.toUtf8());
                        vendorPro.close();
                    }
                }
            }
            if (maked)
                qDebug()<<"\tdone";
            else if (!found)
                qCritical()<<"\tFAILLURE: not found package";
            else
                qCritical()<<"\tFAILLURE";
            qDebug()<<"";
            globalResult *= maked;
        }
    }

    if (!globalResult) {
        qCritical()<<"FAILLURE";
        return 1;
    }
    qDebug()<<"OK";
    return 0;
}
