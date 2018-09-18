/*********************************************************************************
**
** Copyright (c) 2017 The University of Notre Dame
** Copyright (c) 2017 The Regents of the University of California
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
** list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice, this
** list of conditions and the following disclaimer in the documentation and/or other
** materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its contributors may
** be used to endorse or promote products derived from this software without specific
** prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
** EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
** SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
** IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
**
***********************************************************************************/

// Contributors:
// Written by Peter Sempolinski, for the Natural Hazard Modeling Laboratory, director: Ahsan Kareem, at Notre Dame

#include "agavehandler.h"

#include "agavetaskguide.h"
#include "agavetaskreply.h"

#include "filemetadata.h"

//TODO: need to do more double checking of valid file paths

AgaveHandler::AgaveHandler(QNetworkAccessManager *netAccessManager, QObject *parent) :
        RemoteDataInterface(parent), SSLoptions()
{
    networkHandle = netAccessManager;
    SSLoptions.setProtocol(QSsl::SecureProtocols);
    changeAuthState(RemoteDataInterfaceState::INIT);

    if (networkHandle == nullptr)
    {
        qFatal("Internal ERROR: AgaveHandler pointer to QNetworkAccessManager connot be NULL");
        return;
    }
}

void AgaveHandler::finishedOneTask()
{
    pendingRequestCount--;
    if (pendingRequestCount < 0)
    {
        qCDebug(remoteInterface, "Internal Error: Request count becoming negative");
        pendingRequestCount = 0;
    }
    if (pendingRequestCount == 0)
    {
        if (currentState == RemoteDataInterfaceState::DISCONNECTING)
        {
            changeAuthState(RemoteDataInterfaceState::DISCONNECTED);
        }
    }
}

AgaveHandler::~AgaveHandler()
{
    if ((currentState != RemoteDataInterfaceState::DISCONNECTED) &&
            (currentState != RemoteDataInterfaceState::INIT) &&
            (currentState != RemoteDataInterfaceState::READY_TO_AUTH))
    {
        qCDebug(remoteInterface, "ERROR: Agave Handler destroyed without proper shutdown");
    }
    foreach (AgaveTaskGuide * aTaskGuide , validTaskList)
    {
        delete aTaskGuide;
    }
}

QString AgaveHandler::getUserName()
{
    if (QThread::currentThread() != this->thread())
    {
        QString retVal;
        QMetaObject::invokeMethod(this, "getUserName", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QString, retVal));
        return retVal;
    }

    if (currentState == RemoteDataInterfaceState::CONNECTED)
    {
        return authUname;
    }
    return QString();
}

RemoteDataReply * AgaveHandler::performAuth(QString uname, QString passwd)
{   
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "performAuth", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, uname),
                                  Q_ARG(QString, passwd));
        return retVal;
    }

    if (currentState != RemoteDataInterfaceState::READY_TO_AUTH)
    {
        qCDebug(remoteInterface, "Login attempted in wrong state.");
        return createDirectReply("fullAuth", RequestState::INVALID_STATE);
    }
    changeAuthState(RemoteDataInterfaceState::AUTH_TRY);

    authUname = uname;
    authPass = passwd;

    authEncoded = "Basic ";
    QByteArray rawAuth(uname.toLatin1());
    rawAuth.append(":");
    rawAuth.append(passwd);
    authEncoded.append(rawAuth.toBase64());

    AgaveTaskReply * parentReply = new AgaveTaskReply(retriveTaskGuide("fullAuth"),nullptr,this,qobject_cast<QObject *>(this));
    QMap<QString, QByteArray> taskVars;
    parentReply->getTaskParamList()->insert("uname", uname.toLatin1());
    parentReply->getTaskParamList()->insert("passwd", passwd.toLatin1());
    performAgaveQuery("authStep1", taskVars, parentReply);

    return qobject_cast<RemoteDataReply *>(parentReply);
}

RemoteDataReply * AgaveHandler::remoteLS(QString dirPath)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "remoteLS", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, dirPath));
        return retVal;
    }

    if ((dirPath.isEmpty()) || (dirPath == ""))
    {
        dirPath = "/";
    }
    if (!remotePathStringIsValid(dirPath)) return createDirectReply("dirListing", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("dirListing", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("dirPath", dirPath.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("dirListing", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::deleteFile(QString toDelete)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "deleteFile", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, toDelete));
        return retVal;
    }

    if (!remotePathStringIsValid(toDelete)) return createDirectReply("fileDelete", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("fileDelete", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("toDelete", toDelete.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("fileDelete", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::moveFile(QString from, QString to)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "moveFile", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, from),
                                  Q_ARG(QString, to));
        return retVal;
    }

    if (!remotePathStringIsValid(from)) return createDirectReply("fileMove", RequestState::INVALID_PARAM);
    if (!remotePathStringIsValid(to)) return createDirectReply("fileMove", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("fileMove", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("from", from.toLatin1());
    taskVars.insert("to", to.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("fileMove", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::copyFile(QString from, QString to)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "copyFile", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, from),
                                  Q_ARG(QString, to));
        return retVal;
    }

    if (!remotePathStringIsValid(from)) return createDirectReply("fileCopy", RequestState::INVALID_PARAM);
    if (!remotePathStringIsValid(to)) return createDirectReply("fileCopy", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("fileCopy", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("from", from.toLatin1());
    taskVars.insert("to", to.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("fileCopy", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::renameFile(QString fullName, QString newName)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "renameFile", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, fullName),
                                  Q_ARG(QString, newName));
        return retVal;
    }

    if (!remotePathStringIsValid(fullName)) return createDirectReply("renameFile", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("renameFile", RequestState::INVALID_STATE);
    //TODO: check newName is valid

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("fullName", fullName.toLatin1());
    taskVars.insert("newName", newName.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("renameFile", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::mkRemoteDir(QString location, QString newName)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "mkRemoteDir", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, location),
                                  Q_ARG(QString, newName));
        return retVal;
    }

    if (!remotePathStringIsValid(location)) return createDirectReply("newFolder", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("newFolder", RequestState::INVALID_STATE);
    //TODO: check newName is valid

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("location", location.toLatin1());
    taskVars.insert("newName", newName.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("newFolder", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::uploadFile(QString location, QString localFileName)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "uploadFile", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, location),
                                  Q_ARG(QString, localFileName));
        return retVal;
    }

    if (!remotePathStringIsValid(location)) return createDirectReply("fileUpload", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("fileUpload", RequestState::INVALID_STATE);
    //TODO: check that local file exists

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("location", location.toLatin1());
    taskVars.insert("localFileName", localFileName.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("fileUpload", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::uploadBuffer(QString location, QByteArray fileData, QString newFileName)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "uploadBuffer", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, location),
                                  Q_ARG(QByteArray, fileData),
                                  Q_ARG(QString, newFileName));
        return retVal;
    }

    if (!remotePathStringIsValid(location)) return createDirectReply("filePipeUpload", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("filePipeUpload", RequestState::INVALID_STATE);
    //TODO: check newFileName is valid

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("location", location.toLatin1());
    taskVars.insert("newFileName", newFileName.toLatin1());
    taskVars.insert("fileData", fileData);

    AgaveTaskReply * theReply = performAgaveQuery("filePipeUpload",taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::downloadFile(QString localDest, QString remoteName)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "downloadFile", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, localDest),
                                  Q_ARG(QString, remoteName));
        return retVal;
    }

    if (!remotePathStringIsValid(remoteName)) return createDirectReply("fileDownload", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("fileDownload", RequestState::INVALID_STATE);
    //TODO: check localDest exists

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("remoteName", remoteName.toLatin1());
    taskVars.insert("localDest", localDest.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("fileDownload", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::downloadBuffer(QString remoteName)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "downloadBuffer", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, remoteName));
        return retVal;
    }

    if (!remotePathStringIsValid(remoteName)) return createDirectReply("filePipeDownload", RequestState::INVALID_PARAM);
    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("filePipeDownload", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("remoteName", remoteName.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("filePipeDownload", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

AgaveTaskReply * AgaveHandler::getAgaveAppList()
{
    if (QThread::currentThread() != this->thread())
    {
        AgaveTaskReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "getAgaveAppList", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(AgaveTaskReply *, retVal));
        return retVal;
    }

    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("getAgaveList", RequestState::INVALID_STATE);

    return performAgaveQuery("getAgaveList");
}

void AgaveHandler::setAgaveConnectionParams(QString tenant, QString clientId, QString storage)
{
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, "setAgaveConnectionParams", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, tenant),
                                  Q_ARG(QString, clientId),
                                  Q_ARG(QString, storage));
        return;
    }

    if (currentState != RemoteDataInterfaceState::INIT)
    {
        qCDebug(remoteInterface, "ERROR: Can only set connection parameters once per agave handle instance.");
        return;
    }

    tenantURL = tenant;
    clientName = clientId;
    storageNode = storage;

    setupTaskGuideList();

    changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
}

RemoteDataReply * AgaveHandler::runRemoteJob(QString jobName, ParamMap jobParameters, QString remoteWorkingDir, QString indivJobName, QString archivePath)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "runRemoteJob", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, jobName),
                                  Q_ARG(ParamMap, jobParameters),
                                  Q_ARG(QString, remoteWorkingDir),
                                  Q_ARG(QString, indivJobName),
                                  Q_ARG(QString, archivePath));
        return retVal;
    }

    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("agaveAppStart", RequestState::INVALID_STATE);

    //This function is only for Agave Jobs
    AgaveTaskGuide * guideToCheck = retriveTaskGuide(jobName);
    if (guideToCheck == nullptr)
    {
        qCDebug(remoteInterface, "ERROR: Agave App not configured");
        return createDirectReply("agaveAppStart", RequestState::UNKNOWN_TASK);
    }
    if (guideToCheck->getRequestType() != AgaveRequestType::AGAVE_APP)
    {
        qCDebug(remoteInterface, "ERROR: Agave App not configured as Agave App");
        return createDirectReply("agaveAppStart", RequestState::UNKNOWN_TASK);
    }
    QString fullAgaveName = guideToCheck->getAgaveFullName();
    if (fullAgaveName.isEmpty())
    {
        qCDebug(remoteInterface, "ERROR: Agave App does not have a full name");
        return createDirectReply("agaveAppStart", RequestState::INTERNAL_ERROR);
    }
    if (!remotePathStringIsValid(remoteWorkingDir)) return createDirectReply(guideToCheck, RequestState::INVALID_PARAM);

    QJsonDocument rawJSONinput;
    QJsonObject rootObject;
    rootObject.insert("appId",QJsonValue(fullAgaveName));
    if (indivJobName.isEmpty())
    {
        rootObject.insert("name",QJsonValue(fullAgaveName.append("-run")));
    }
    else
    {
        rootObject.insert("name",QJsonValue(indivJobName));
    }

    if (!archivePath.isEmpty())
    {
        rootObject.insert("archivePath",QJsonValue(archivePath));
    }

    QJsonObject inputList;
    QJsonObject paramList;

    QStringList expectedInputs = guideToCheck->getAgaveInputList();
    QStringList expectedParams = guideToCheck->getAgaveParamList();

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("jobName", jobName.toLatin1());

    if ((!guideToCheck->getAgavePWDparam().isEmpty()) && (!remoteWorkingDir.isEmpty()))
    {
        jobParameters.insert(guideToCheck->getAgavePWDparam(),remoteWorkingDir);
        taskVars.insert("remoteWorkingDir", remoteWorkingDir.toLatin1());
    }

    for (auto itr = jobParameters.cbegin(); itr != jobParameters.cend(); itr++)
    {
        taskVars.insert(itr.key(), (*itr).toLatin1());

        QJsonObject * objectToAddTo;

        if (expectedParams.contains(itr.key()))
        {
            objectToAddTo = &paramList;
        }
        else if (expectedInputs.contains(itr.key()))
        {
            objectToAddTo = &inputList;
        }
        else
        {
            qCDebug(remoteInterface, "ERROR: Agave App given invalid parameter");
            return createDirectReply(guideToCheck, RequestState::INVALID_PARAM);
        }

        objectToAddTo->insert(itr.key(),QJsonValue(*itr));
    }
    QJsonValue inputListValue(inputList);
    QJsonValue paramListValue(paramList);
    rootObject.insert("inputs",inputListValue);
    rootObject.insert("parameters",paramListValue);
    rawJSONinput.setObject(rootObject);

    taskVars.insert("rawJSONinput", rawJSONinput.toJson());
    taskVars.insert("fileData", rawJSONinput.toJson());

    qCDebug(remoteInterface, "%s",qPrintable(rawJSONinput.toJson()));

    AgaveTaskReply * theReply = performAgaveQuery("agaveAppStart", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::runAgaveJob(QJsonDocument rawJobJSON)
{
    if (QThread::currentThread() != this->thread())
    {
        AgaveTaskReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "runAgaveJob", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(AgaveTaskReply *, retVal),
                                  Q_ARG(QJsonDocument, rawJobJSON));
        return retVal;
    }

    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("agaveAppStart", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;

    taskVars.insert("rawJobJSON", rawJobJSON.toJson());
    taskVars.insert("fileData", rawJobJSON.toJson());

    qCDebug(remoteInterface, "%s",qPrintable(rawJobJSON.toJson()));

    AgaveTaskReply * theReply = performAgaveQuery("agaveAppStart", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::getListOfJobs()
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "getListOfJobs", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal));
        return retVal;
    }

    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("getJobList", RequestState::INVALID_STATE);

    return qobject_cast<RemoteDataReply *>(performAgaveQuery("getJobList"));
}

RemoteDataReply * AgaveHandler::getJobDetails(QString IDstr)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "getJobDetails", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, IDstr));
        return retVal;
    }

    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("getJobDetails", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("IDstr", IDstr.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("getJobDetails", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::stopJob(QString IDstr)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "stopJob", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, IDstr));
        return retVal;
    }

    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("stopJob", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("IDstr", IDstr.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("stopJob", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataReply * AgaveHandler::deleteJob(QString IDstr)
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "deleteJob", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal),
                                  Q_ARG(QString, IDstr));
        return retVal;
    }

    if (currentState != RemoteDataInterfaceState::CONNECTED) return createDirectReply("deleteJob", RequestState::INVALID_STATE);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("IDstr", IDstr.toLatin1());

    AgaveTaskReply * theReply = performAgaveQuery("deleteJob", taskVars);
    return qobject_cast<RemoteDataReply *>(theReply);
}

RemoteDataInterfaceState AgaveHandler::getInterfaceState()
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataInterfaceState retVal;
        QMetaObject::invokeMethod(this, "getInterfaceState", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataInterfaceState, retVal));
        return retVal;
    }

    return currentState;
}

void AgaveHandler::registerAgaveAppInfo(QString agaveAppName, QString fullAgaveName, QStringList parameterList, QStringList inputList, QString workingDirParameter)
{
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, "registerAgaveAppInfo", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, agaveAppName),
                                  Q_ARG(QString, fullAgaveName),
                                  Q_ARG(QStringList, parameterList),
                                  Q_ARG(QStringList, inputList),
                                  Q_ARG(QString, workingDirParameter));
        return;
    }

    qCDebug(remoteInterface, "Registering Agave ID: %s", qPrintable(fullAgaveName));
    AgaveTaskGuide * toInsert = new AgaveTaskGuide(agaveAppName, AgaveRequestType::AGAVE_APP);
    toInsert->setAgaveFullName(fullAgaveName);
    toInsert->setAgaveParamList(parameterList);
    toInsert->setAgaveInputList(inputList);
    toInsert->setAgavePWDparam(workingDirParameter);
    insertAgaveTaskGuide(toInsert);
}

RemoteDataReply * AgaveHandler::closeAllConnections()
{
    if (QThread::currentThread() != this->thread())
    {
        RemoteDataReply * retVal = nullptr;
        QMetaObject::invokeMethod(this, "closeAllConnections", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(RemoteDataReply *, retVal));
        return retVal;
    }

    if ((currentState == RemoteDataInterfaceState::INIT) || (currentState == RemoteDataInterfaceState::READY_TO_AUTH) ||
            (currentState == RemoteDataInterfaceState::DISCONNECTED))
    {
        qCDebug(remoteInterface, "ERROR: Logout attempted when not logged in.");
        changeAuthState(RemoteDataInterfaceState::DISCONNECTED);
        return createDirectReply("startedLogout", RequestState::INVALID_STATE);
    }

    if ((currentState == RemoteDataInterfaceState::CANCEL_AUTH) || (currentState == RemoteDataInterfaceState::DISCONNECTING))
    {
        qCDebug(remoteInterface, "ERROR: Duplicate Logout attempts detected.");
        return createDirectReply("startedLogout", RequestState::INVALID_STATE);
    }

    if (currentState == RemoteDataInterfaceState::AUTH_TRY)
    {
        qCDebug(remoteInterface, "Cancelling Agave Login");
        changeAuthState(RemoteDataInterfaceState::CANCEL_AUTH);
        return createDirectReply("startedLogout", RequestState::GOOD);
    }

    if (clientEncoded.isEmpty() || token.isEmpty())
    {
        qCDebug(remoteInterface, "ERROR: Logout attempted incomplete login info.");
        changeAuthState(RemoteDataInterfaceState::DISCONNECTED);
        return createDirectReply("startedLogout", RequestState::INTERNAL_ERROR);
    }

    qCDebug(remoteInterface, "Closing agave connection.");
    changeAuthState(RemoteDataInterfaceState::DISCONNECTING);

    QMap<QString, QByteArray> taskVars;
    taskVars.insert("token", token);
    performAgaveQuery("authRevoke", taskVars);
    //maybe TODO: Remove client entry?

    if (noPendingHttpRequests())
    {
        qCDebug(remoteInterface, "ERROR: Logout never creates revoke task");
        changeAuthState(RemoteDataInterfaceState::DISCONNECTED);
        return createDirectReply("startedLogout", RequestState::INTERNAL_ERROR);
    }

    return createDirectReply("startedLogout", RequestState::GOOD);
}

void AgaveHandler::changeAuthState(RemoteDataInterfaceState newState)
{
    currentState = newState;

    if ((currentState == RemoteDataInterfaceState::READY_TO_AUTH)
            || (currentState == RemoteDataInterfaceState::INIT)
            || (currentState == RemoteDataInterfaceState::DISCONNECTED))
    {
        authEncoded = "";
        clientEncoded = "";
        token = "";
        refreshToken = "";

        authUname = "";
        authPass = "";
        clientKey = "";
        clientSecret = "";
    }

    emit connectionStateChanged(currentState);
}

void AgaveHandler::setupTaskGuideList()
{
    AgaveTaskGuide * toInsert = nullptr;

    toInsert = new AgaveTaskGuide("fullAuth", AgaveRequestType::AGAVE_NONE);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("startedLogout", AgaveRequestType::AGAVE_NONE);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authStep1", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix(QString("/clients/v2/%1").arg(clientName));
    toInsert->setHeaderType(AuthHeaderType::PASSWD);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authStep1a", AgaveRequestType::AGAVE_DELETE);
    toInsert->setURLsuffix(QString("/clients/v2/%1").arg(clientName));
    toInsert->setHeaderType(AuthHeaderType::PASSWD);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authStep2", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/clients/v2/"));
    toInsert->setHeaderType(AuthHeaderType::PASSWD);
    toInsert->setPostParams(QString("clientName=%1&description=Client ID for SimCenter Wind GUI App").arg(clientName));
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authStep3", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/token"));
    toInsert->setHeaderType(AuthHeaderType::CLIENT);
    toInsert->setPostParams("username=%1&password=%2&grant_type=password&scope=PRODUCTION", {"authUname", "authPass"});
    toInsert->setTokenFormat(true);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authRefresh", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/token"));
    toInsert->setHeaderType(AuthHeaderType::CLIENT);
    toInsert->setPostParams("grant_type=refresh_token&scope=PRODUCTION&refresh_token=%1",{"token"});
    toInsert->setTokenFormat(true);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authRevoke", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/revoke"));
    toInsert->setHeaderType(AuthHeaderType::CLIENT);
    toInsert->setPostParams("token=%1",{"token"});
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("dirListing", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix((QString("/files/v2/listings/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"dirPath"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileUpload", AgaveRequestType::AGAVE_UPLOAD);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"location"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileDownload", AgaveRequestType::AGAVE_DOWNLOAD);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    //toInsert->setURLsuffix(QString("/files/v2/media/"));
    toInsert->setDynamicURLParams("%1",{"remoteName"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("filePipeUpload", AgaveRequestType::AGAVE_PIPE_UPLOAD);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"location"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("filePipeDownload", AgaveRequestType::AGAVE_PIPE_DOWNLOAD);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"remoteName"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileDelete", AgaveRequestType::AGAVE_DELETE);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"toDelete"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("newFolder", AgaveRequestType::AGAVE_PUT);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"location"});
    toInsert->setPostParams("action=mkdir&path=%1",{"newName"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("renameFile", AgaveRequestType::AGAVE_PUT);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"fullName"});
    toInsert->setPostParams("action=rename&path=%1",{"newName"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileCopy", AgaveRequestType::AGAVE_PUT);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"from"});
    toInsert->setPostParams("action=copy&path=%1",{"to"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileMove", AgaveRequestType::AGAVE_PUT);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",{"from"});
    toInsert->setPostParams("action=move&path=%1",{"to"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("agaveAppStart", AgaveRequestType::AGAVE_PIPE_UPLOAD);
    toInsert->setURLsuffix(QString("/jobs/v2"));
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("getAgaveList", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix(QString("/apps/v2"));
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("getJobList", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix(QString("/jobs/v2"));
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("getJobDetails", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix(QString("/jobs/v2/"));
    toInsert->setDynamicURLParams("%1",{"IDstr"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("stopJob", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/jobs/v2/"));
    toInsert->setDynamicURLParams("%1",{"IDstr"});
    toInsert->setPostParams("action=stop");
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("deleteJob", AgaveRequestType::AGAVE_DELETE);
    toInsert->setURLsuffix(QString("/jobs/v2/"));
    toInsert->setDynamicURLParams("%1",{"IDstr"});
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);
}

void AgaveHandler::insertAgaveTaskGuide(AgaveTaskGuide * newGuide)
{
    QString taskName = newGuide->getTaskID();
    if (validTaskList.contains(taskName))
    {
        qCDebug(remoteInterface, "ERROR: Invalid Task Guide List: Duplicate Name");
        return;
    }
    validTaskList.insert(taskName,newGuide);
}

AgaveTaskGuide * AgaveHandler::retriveTaskGuide(QString taskID)
{
    AgaveTaskGuide * ret;
    if (!validTaskList.contains(taskID))
    {
        qCDebug(remoteInterface, "ERROR: Non-existant request requested.");
        return nullptr;
    }
    ret = validTaskList.value(taskID);
    if (taskID != ret->getTaskID())
    {
        qCDebug(remoteInterface, "ERROR: Task Guide format error.");
    }
    return ret;
}

bool AgaveHandler::remotePathStringIsValid(QString)
{
    //TODO: Check for odd chars, bad syntactical structure and the like
    return true;
}

void AgaveHandler::forwardReplyToParent(AgaveTaskReply * agaveReply, RequestState replyState)
{
    AgaveTaskReply * parentReply = qobject_cast<AgaveTaskReply *>(agaveReply->parent());
    if (parentReply == nullptr)
    {
        qCDebug(remoteInterface, "ERROR: Invalid parent for forwarding task.");
        return;
    }
    parentReply->rawNoDataNoHttpTaskComplete(replyState);
}

bool AgaveHandler::noPendingHttpRequests()
{
    return (pendingRequestCount == 0);
}

void AgaveHandler::handleInternalTask(AgaveTaskReply *agaveReply, RequestState taskState)
{
    if (agaveReply == nullptr)
    {
        qCDebug(remoteInterface, "ERROR: Internal task handler called without AgaveTaskReply object");
        forwardReplyToParent(agaveReply, RequestState::INTERNAL_ERROR);
        return;
    }

    if (agaveReply->getTaskGuide()->getTaskID() == "authRevoke")
    {
        qCDebug(remoteInterface, "Auth revoke procedure complete");
        changeAuthState(RemoteDataInterfaceState::DISCONNECTED);
        return;
    }

    if (taskState == RequestState::GOOD)
    {
        qCDebug(remoteInterface, "ERROR: Internal handler with explicit RequestState should never be GOOD");
        forwardReplyToParent(agaveReply, RequestState::INTERNAL_ERROR);
        return;
    }

    QString taskID = agaveReply->getTaskGuide()->getTaskID();

    if ((taskID == "authStep1") || (taskID == "authStep1a") || (taskID == "authStep2") || (taskID == "authStep3"))
    {
        if (currentState == RemoteDataInterfaceState::CANCEL_AUTH)
        {
            changeAuthState(RemoteDataInterfaceState::DISCONNECTED);
        }
        else
        {
            changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
        }
    }
    forwardReplyToParent(agaveReply, taskState);
}

void AgaveHandler::handleInternalTask(AgaveTaskReply * agaveReply, QNetworkReply * rawReply)
{
    if (agaveReply == nullptr)
    {
        qCDebug(remoteInterface, "ERROR: Internal task handler called without AgaveTaskReply object");
        return;
    }

    if (agaveReply->getTaskGuide()->getTaskID() == "authRevoke")
    {
        qCDebug(remoteInterface, "Auth revoke procedure complete");
        changeAuthState(RemoteDataInterfaceState::DISCONNECTED);
        return;
    }

    const QByteArray replyText = rawReply->readAll();

    QJsonParseError parseError;
    QJsonDocument parseHandler = QJsonDocument::fromJson(replyText, &parseError);

    if (parseHandler.isNull())
    {
        forwardReplyToParent(agaveReply, RequestState::JSON_PARSE_ERROR);
        return;
    }

    qCDebug(rawHTTP, "%s",qPrintable(parseHandler.toJson()));

    RequestState prelimResult = AgaveTaskReply::standardSuccessFailCheck(agaveReply->getTaskGuide(), &parseHandler);

    QString taskID = agaveReply->getTaskGuide()->getTaskID();

    if ((prelimResult != RequestState::GOOD) && (prelimResult != RequestState::EXPLICIT_ERROR))
    {
        if ((taskID == "authStep1") || (taskID == "authStep1a") || (taskID == "authStep2") || (taskID == "authStep3"))
        {
            changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
        }
        forwardReplyToParent(agaveReply, prelimResult);
        return;
    }

    if (taskID == "authStep1")
    {
        if (currentState == RemoteDataInterfaceState::CANCEL_AUTH)
        {
            changeAuthState(RemoteDataInterfaceState::DISCONNECTED);
            forwardReplyToParent(agaveReply, RequestState::INVALID_STATE);
        }
        else if (prelimResult == RequestState::GOOD)
        {
            QMap<QString, QByteArray> varList;
            performAgaveQuery("authStep1a", varList, qobject_cast<AgaveTaskReply *>(agaveReply->parent()));
        }
        else
        {
            QString messageData = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "message").toString();
            if (messageData == "Application not found")
            {
                QMap<QString, QByteArray> varList;
                performAgaveQuery("authStep2", varList, qobject_cast<AgaveTaskReply *>(agaveReply->parent()));
            }
            else if (messageData == "Login failed.Please recheck the username and password and try again.")
            {
                changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
                forwardReplyToParent(agaveReply, RequestState::EXPLICIT_ERROR);
            }
            else
            {
                changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
                forwardReplyToParent(agaveReply, prelimResult);
            }
        }
    }
    else if (taskID == "authStep1a")
    {
        if (prelimResult == RequestState::GOOD)
        {
            QMap<QString, QByteArray> varList;
            performAgaveQuery("authStep2", varList, qobject_cast<AgaveTaskReply *>(agaveReply->parent()));
        }
        else
        {
            changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
            forwardReplyToParent(agaveReply, prelimResult);
        }
    }
    else if (taskID == "authStep2")
    {
        if (prelimResult == RequestState::GOOD)
        {
            clientKey = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, {"result", "consumerKey"}).toString();
            clientSecret = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, {"result", "consumerSecret"}).toString();

            if (clientKey.isEmpty() || clientSecret.isEmpty())
            {
                changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
                forwardReplyToParent(agaveReply, RequestState::JSON_PARSE_ERROR);
                return;
            }

            clientEncoded = "Basic ";
            QByteArray rawAuth(clientKey.toLatin1());
            rawAuth.append(":");
            rawAuth.append(clientSecret);
            clientEncoded.append(rawAuth.toBase64());

            QMap<QString, QByteArray> varList;
            varList.insert("authUname", authUname.toLatin1());
            varList.insert("authPass", authPass.toLatin1());

            performAgaveQuery("authStep3", varList, qobject_cast<AgaveTaskReply *>(agaveReply->parent()));
        }
        else
        {
            changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
            forwardReplyToParent(agaveReply, prelimResult);
        }
    }
    else if (taskID == "authStep3")
    {
        if (prelimResult == RequestState::GOOD)
        {
            token = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "access_token").toString().toLatin1();
            refreshToken = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "refresh_token").toString().toLatin1();

            if (token.isEmpty() || refreshToken.isEmpty())
            {
                changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
                forwardReplyToParent(agaveReply, RequestState::JSON_PARSE_ERROR);
            }
            else
            {
                tokenHeader = (QString("Bearer ").append(token)).toLatin1();

                changeAuthState(RemoteDataInterfaceState::CONNECTED);
                forwardReplyToParent(agaveReply, RequestState::GOOD);
                qCDebug(remoteInterface, "Login success.");
            }
        }
        else
        {
            changeAuthState(RemoteDataInterfaceState::READY_TO_AUTH);
            forwardReplyToParent(agaveReply, prelimResult);
        }
    }
    else if (taskID == "authRefresh")
    {
        qCDebug(remoteInterface, "Auth refresh occurred without being implemented.");
        forwardReplyToParent(agaveReply, RequestState::NOT_IMPLEMENTED);

        /*
        if (prelimResult == RequestState::GOOD)
        {
            token = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "access_token").toString().toLatin1();
            refreshToken = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "refresh_token").toString().toLatin1();

            if (token.isEmpty() || refreshToken.isEmpty())
            {
                emit sendFatalErrorMessage("Token refresh failure.");
                return;
            }
            //TODO: Will need more info here based on when, how and where refreshes are requested
        }
        */
    }
    else
    {
        qCDebug(remoteInterface, "Non-existant internal request requested.");
        forwardReplyToParent(agaveReply, RequestState::UNKNOWN_TASK);
    }
}

AgaveTaskReply * AgaveHandler::performAgaveQuery(QString queryName)
{
    QMap<QString, QByteArray> taskVars;
    return performAgaveQuery(queryName, taskVars);
}

AgaveTaskReply * AgaveHandler::performAgaveQuery(QString queryName, QMap<QString, QByteArray> varList, AgaveTaskReply * parentReq)
{
    //The network availabilty flag seems innacurate cross-platform
    /*
    if (networkHandle.networkAccessible() == QNetworkAccessManager::NotAccessible){}
    */

    if ((currentState == RemoteDataInterfaceState::CANCEL_AUTH) ||
            (currentState == RemoteDataInterfaceState::DISCONNECTED) ||
            (currentState == RemoteDataInterfaceState::DISCONNECTING))
    {
        if (queryName != "authRevoke")
        {
            qCDebug(remoteInterface, "Rejecting request given during shutdown.");
            return createDirectReply(queryName, RequestState::INVALID_STATE, parentReq);
        }
    }

    AgaveTaskGuide * taskGuide = retriveTaskGuide(queryName);

    if ((currentState != RemoteDataInterfaceState::CONNECTED) &&
            (taskGuide->getHeaderType() == AuthHeaderType::TOKEN))
    {
        qCDebug(remoteInterface, "Rejecting request prior to connection established.");
        return createDirectReply(taskGuide, RequestState::INVALID_STATE, parentReq);
    }

    QNetworkReply * qReply = distillRequestData(taskGuide, &varList);

    if (qReply == nullptr)
    {
        return createDirectReply(taskGuide, RequestState::INTERNAL_ERROR, parentReq);
    }
    pendingRequestCount++;

    QObject * parentObj = qobject_cast<QObject *>(this);
    if (parentReq != nullptr) parentObj = qobject_cast<QObject *>(parentReq);

    AgaveTaskReply * ret = new AgaveTaskReply(taskGuide,qReply,this, parentObj);

    for (auto itr = varList.cbegin(); itr != varList.cend(); itr++)
    {
        ret->getTaskParamList()->insert(itr.key(), *itr);
    }

    return ret;
}

AgaveTaskReply * AgaveHandler::createDirectReply(QString theTaskType, RequestState errorState, AgaveTaskReply * parentReq)
{
    return createDirectReply(retriveTaskGuide(theTaskType), errorState, parentReq);
}

AgaveTaskReply * AgaveHandler::createDirectReply(AgaveTaskGuide * theTaskType, RequestState errorState, AgaveTaskReply * parentReq)
{
    QObject * parentObj = qobject_cast<QObject *>(this);
    if (parentReq != nullptr) parentObj = qobject_cast<QObject *>(parentReq);
    return new AgaveTaskReply(theTaskType, errorState, this, parentObj);
}

QNetworkReply * AgaveHandler::distillRequestData(AgaveTaskGuide * taskGuide, QMap<QString, QByteArray> * varList)
{
    QByteArray * authHeader = nullptr;
    if (taskGuide->getHeaderType() == AuthHeaderType::CLIENT)
    {
        authHeader = &clientEncoded;
    }
    else if (taskGuide->getHeaderType() == AuthHeaderType::PASSWD)
    {
        authHeader = &authEncoded;
    }
    else if (taskGuide->getHeaderType() == AuthHeaderType::REFRESH)
    {
        //TODO
    }
    else if (taskGuide->getHeaderType() == AuthHeaderType::TOKEN)
    {
        authHeader = &tokenHeader;
    }

    if ((taskGuide->getRequestType() == AgaveRequestType::AGAVE_POST) || (taskGuide->getRequestType() == AgaveRequestType::AGAVE_PUT))
    {
        //Note: For a put, the post data for this function is used as the put data for the HTTP request
        return finalizeAgaveRequest(taskGuide, taskGuide->getArgAndURLsuffix(varList),
                         authHeader, taskGuide->fillPostArgList(varList));
    }
    else if ((taskGuide->getRequestType() == AgaveRequestType::AGAVE_GET) || (taskGuide->getRequestType() == AgaveRequestType::AGAVE_DELETE))
    {
        qCDebug(remoteInterface, "URL Req: %s", qPrintable(taskGuide->getArgAndURLsuffix(varList)));
        return finalizeAgaveRequest(taskGuide, taskGuide->getArgAndURLsuffix(varList),
                         authHeader);
    }
    else if (taskGuide->getRequestType() == AgaveRequestType::AGAVE_UPLOAD)
    {
        //For agave upload, instead of post params, we have the full local file name
        QString fullFileName = QString::fromLatin1(varList->value("localFileName"));
        QFile * fileHandle = new QFile(fullFileName);
        if (!fileHandle->open(QIODevice::ReadOnly))
        {
            fileHandle->deleteLater();
            return nullptr;
        }
        qCDebug(remoteInterface, "URL Req: %s", qPrintable(taskGuide->getArgAndURLsuffix(varList)));

        return finalizeAgaveRequest(taskGuide, taskGuide->getArgAndURLsuffix(varList),
                         authHeader, fullFileName.toLatin1(), fileHandle);
    }
    else if (taskGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_UPLOAD)
    {
        qCDebug(remoteInterface, "New File Name: %s\n", qPrintable(varList->value("newFileName")));

        QBuffer * pipedData = new QBuffer();
        pipedData->open(QBuffer::ReadWrite);
        pipedData->write(varList->value("fileData"));
        pipedData->seek(0);

        qCDebug(remoteInterface, "URL Req: %s", qPrintable(taskGuide->getArgAndURLsuffix(varList)));

        return finalizeAgaveRequest(taskGuide, taskGuide->getArgAndURLsuffix(varList),
                         authHeader, varList->value("newFileName"), pipedData);
    }
    else if (taskGuide->getRequestType() == AgaveRequestType::AGAVE_DOWNLOAD)
    {
        //For agave download, instead of post params, we have the full local file name
        QString fullFileName = varList->value("localDest");
        QFile * fileHandle = new QFile(fullFileName);
        if (fileHandle->open(QIODevice::ReadOnly))
        {
            //If the file already exists, we do not overwrite
            //This should be checked by calling client, but we check it here too
            fileHandle->deleteLater();
            return nullptr;
        }
        fileHandle->deleteLater();
        qCDebug(remoteInterface, "URL Req: %s", qPrintable(taskGuide->getArgAndURLsuffix(varList)));

        return finalizeAgaveRequest(taskGuide, taskGuide->getArgAndURLsuffix(varList),
                         authHeader);
    }
    else if (taskGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_DOWNLOAD)
    {
        return finalizeAgaveRequest(taskGuide, taskGuide->getArgAndURLsuffix(varList), authHeader);
    }
    else
    {
        qCDebug(remoteInterface, "ERROR: Non-existant Agave request type requested.");
        return nullptr;
    }

    return nullptr;
}

QNetworkReply * AgaveHandler::finalizeAgaveRequest(AgaveTaskGuide * theGuide, QString urlAppend, QByteArray * authHeader, QByteArray postData, QIODevice * fileHandle)
{
    QNetworkReply * clientReply = nullptr;

    QString activeURL = tenantURL;
    activeURL.append(removeDoubleSlashes(urlAppend));

    QNetworkRequest * clientRequest = new QNetworkRequest();
    clientRequest->setUrl(QUrl(activeURL));

    //clientRequest->setRawHeader("User-Agent", "SimCenterWindGUI");
    if (theGuide->getRequestType() == AgaveRequestType::AGAVE_POST)
    {
        clientRequest->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    }

    if (authHeader != nullptr)
    {
        if (authHeader->isEmpty())
        {
            qCDebug(remoteInterface, "ERROR: Authorization request reply has no data in it");
            if (fileHandle != nullptr) fileHandle->deleteLater();
            return nullptr;
        }
        clientRequest->setRawHeader(QByteArray("Authorization"), *authHeader);
    }

    clientRequest->setSslConfiguration(SSLoptions);

    qCDebug(remoteInterface, "%s", qPrintable(clientRequest->url().url()));

    if ((theGuide->getRequestType() == AgaveRequestType::AGAVE_GET) || (theGuide->getRequestType() == AgaveRequestType::AGAVE_DOWNLOAD)
            || (theGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_DOWNLOAD))
    {
        clientReply = networkHandle->get(*clientRequest);
    }
    else if (theGuide->getRequestType() == AgaveRequestType::AGAVE_POST)
    {
        clientReply = networkHandle->post(*clientRequest, postData);
    }
    else if (theGuide->getRequestType() == AgaveRequestType::AGAVE_PUT)
    {
        clientReply = networkHandle->put(*clientRequest, postData);
    }
    else if (theGuide->getRequestType() == AgaveRequestType::AGAVE_DELETE)
    {
        clientReply = networkHandle->deleteResource(*clientRequest);
    }
    else if ((theGuide->getRequestType() == AgaveRequestType::AGAVE_UPLOAD) || (theGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_UPLOAD))
    {
        QHttpMultiPart * fileUpload = new QHttpMultiPart(QHttpMultiPart::FormDataType);

        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-strem"));
        QString tempString = "form-data; name=\"fileToUpload\"; filename=\"%1\"";
        tempString = tempString.arg(QString(postData));
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(tempString));

        filePart.setBodyDevice(fileHandle);
        //Following line is to insure deletion of the file handle later, when the parent is deleted
        fileHandle->setParent(fileUpload);

        fileUpload->append(filePart);

        clientReply = networkHandle->post(*clientRequest, fileUpload);

        //Following line insures Mulipart object deleted when the network reply is
        fileUpload->setParent(clientReply);
    }

    QObject::connect(clientReply, SIGNAL(finished()), this, SLOT(finishedOneTask()), Qt::QueuedConnection);

    return clientReply;
}
