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

#include "agavetaskreply.h"

#include "agavehandler.h"
#include "agavetaskguide.h"

#include "filemetadata.h"
#include "remotejobdata.h"

AgaveTaskReply::AgaveTaskReply(AgaveTaskGuide * theGuide, QNetworkReply * newReply, AgaveHandler *theManager, QObject *parent) : RemoteDataReply(parent)
{
    if (!performInitPointerCheck(theGuide, theManager)) return;
    myReplyObject = newReply;

    if ((myGuide->getRequestType() == AgaveRequestType::AGAVE_NONE) && (myReplyObject == nullptr))
    {
        return;
    }

    if ((myGuide->getRequestType() == AgaveRequestType::AGAVE_NONE) && (myReplyObject != nullptr))
    {
        qCDebug(remoteInterface, "Agave Task type that does not use QNetworkReply improperly given a QNetworkReply");
        myReplyObject->deleteLater();
        setDelayedDatalessReply(RequestState::INTERNAL_ERROR);
        return;
    }

    if (myReplyObject != nullptr)
    {
        QObject::connect(myReplyObject, SIGNAL(finished()), this, SLOT(rawHttpTaskComplete()));
    }
    else
    {
        qCDebug(remoteInterface, "Agave Task type that uses QNetworkReply not given a QNetworkReply");
        setDelayedDatalessReply(RequestState::INTERNAL_ERROR);
    }
}

AgaveTaskReply::AgaveTaskReply(AgaveTaskGuide * theGuide, RequestState passThruErrorState, AgaveHandler * theManager, QObject *parent) : RemoteDataReply(parent)
{
    if (!performInitPointerCheck(theGuide, theManager)) return;
    setDelayedDatalessReply(passThruErrorState);
}

bool AgaveTaskReply::performInitPointerCheck(AgaveTaskGuide * theGuide, AgaveHandler * theManager)
{
    myManager = theManager;
    myGuide = theGuide;

    if (myManager == nullptr)
    {
        setDelayedDatalessReply(RequestState::INTERNAL_ERROR);
        return false;
    }

    if (myGuide == nullptr)
    {
        setDelayedDatalessReply(RequestState::UNKNOWN_TASK);
        return false;
    }
    return true;
}

AgaveTaskReply::~AgaveTaskReply()
{
    if (myReplyObject != nullptr)
    {
        myReplyObject->deleteLater();
    }
}

QMap<QString, QByteArray> * AgaveTaskReply::getTaskParamList()
{
    return &taskParamList;
}

void AgaveTaskReply::setAsUnconnectedReply()
{
    expectsSignalConnect = false;
}

void AgaveTaskReply::setDelayedDatalessReply(RequestState replyState)
{
    pendingReply = replyState;

    QTimer * quickTimer = new QTimer(qobject_cast<QObject*>(this));
    QObject::connect(quickTimer, SIGNAL(timeout()), this, SLOT(rawPassThruTaskComplete()));
    quickTimer->start(1);
}

AgaveTaskGuide * AgaveTaskReply::getTaskGuide()
{
    return myGuide;
}

void AgaveTaskReply::processDatalessReply(RequestState replyState)
{   
    if (replyState != RequestState::GOOD)
    {
        qCDebug(remoteInterface, "Agave Task Fail: %s", qPrintable(RemoteDataInterface::interpretRequestState(replyState)));
    }

    if (myGuide->getTaskID() == "fullAuth")
    {
        emit haveAuthReply(replyState);
    }
    else if (myGuide->getTaskID() == "authRefresh")
    {
        //TODO: Auth refresh needs to be implemented
        qCDebug(remoteInterface, "Auth refresh fail: Not yet implemented");
        return;
    }
    else if (myGuide->getTaskID() == "dirListing")
    {
        emit haveLSReply(replyState, QList<FileMetaData>());
    }
    else if (myGuide->getTaskID() == "startedLogout")
    {
        emit startedLogout(replyState);
    }
    else if ((myGuide->getTaskID() == "fileUpload") || (myGuide->getTaskID() == "filePipeUpload"))
    {
        emit haveUploadReply(replyState, FileMetaData());
    }
    else if (myGuide->getTaskID() == "fileDelete")
    {
        emit haveDeleteReply(replyState, QString());
    }
    else if (myGuide->getTaskID() == "newFolder")
    {
        emit haveMkdirReply(replyState, FileMetaData());
    }
    else if (myGuide->getTaskID() == "renameFile")
    {
        emit haveRenameReply(replyState, FileMetaData(), QString());
    }
    else if (myGuide->getTaskID() == "fileMove")
    {
        emit haveMoveReply(replyState, FileMetaData(), QString());
    }
    else if (myGuide->getTaskID() == "fileCopy")
    {
        emit haveCopyReply(replyState,FileMetaData());
    }
    else if (myGuide->getTaskID() == "fileDownload")
    {
        emit haveDownloadReply(replyState, QString());
    }
    else if (myGuide->getTaskID() == "filePipeDownload")
    {
        emit haveBufferDownloadReply(replyState, nullptr);
    }
    else if (myGuide->getTaskID() == "getJobList")
    {
        emit haveJobList(replyState, QList<RemoteJobData>());
    }
    else if (myGuide->getTaskID() == "getJobDetails")
    {
        emit haveJobDetails(replyState, RemoteJobData::nil());
    }
    else if (myGuide->getTaskID() == "stopJob")
    {
        emit haveStoppedJob(replyState);
    }
    else if (myGuide->getTaskID() == "deleteJob")
    {
        emit haveDeletedJob(replyState);
    }
    else if (myGuide->getTaskID() == "haveAgaveAppList")
    {
        emit haveAgaveAppList(replyState, QVariantList());
    }
    else
    {
        emit haveJobReply(replyState, QJsonDocument());
    }

}

void AgaveTaskReply::rawNoDataNoHttpTaskComplete(RequestState replyState)
{
    this->deleteLater();

    if (myGuide->getRequestType() != AgaveRequestType::AGAVE_NONE)
    {
        qCDebug(remoteInterface, "ERROR: no-http no-data reply signaled for wrong task type");
        processDatalessReply(RequestState::INTERNAL_ERROR);
    }

    pendingReply = replyState;
    rawPassThruTaskComplete();
}

void AgaveTaskReply::rawPassThruTaskComplete()
{
    this->deleteLater();

    //If this task is an INTERNAL task, then the result is redirected to the manager
    if (myGuide->isInternal())
    {
        myManager->handleInternalTask(this, pendingReply);
        return;
    }

    signalConnectDelay();
    processDatalessReply(pendingReply);
}

void AgaveTaskReply::rawHttpTaskComplete()
{
    this->deleteLater();

    //If this task is an INTERNAL task, then the result is redirected to the manager
    if (myGuide->isInternal())
    {
        myManager->handleInternalTask(this, myReplyObject);
        return;
    }

    signalConnectDelay();

    if (myReplyObject == nullptr)
    {
        qCDebug(remoteInterface, "ERROR: http reply signaled without http request");
        processDatalessReply(RequestState::INTERNAL_ERROR);
        return;
    }

    if (myGuide->getTaskID() != "authRevoke")
    {
        if ((myManager->getInterfaceState() == RemoteDataInterfaceState::DISCONNECTING) ||
                (myManager->getInterfaceState() == RemoteDataInterfaceState::DISCONNECTED))
        {
            qCDebug(remoteInterface, "Request during disconnect ignored");
            processDatalessReply(RequestState::INVALID_STATE);
            return;
        }
    }

    QNetworkReply * testReply = qobject_cast<QNetworkReply *>(sender());
    if (testReply != myReplyObject)
    {
        processDatalessReply(RequestState::SIGNAL_OBJ_MISMATCH);
        return;
    }    

    if (testReply->error() != QNetworkReply::NoError)
    {
        if (testReply->error() == 403)
        {
            processDatalessReply(RequestState::SERVICE_UNAVAILABLE);
        }
        else if (testReply->error() == 401)
        {
            processDatalessReply(RequestState::REMOTE_SERVER_ERROR);
        }
        else if (testReply->error() == 3)
        {
            processDatalessReply(RequestState::LOST_INTERNET);
        }
        else if (testReply->error() == 2)
        {
            processDatalessReply(RequestState::DROPPED_CONNECTION);
        }
        else if (testReply->error() == 203)
        {
            processDatalessReply(RequestState::FILE_NOT_FOUND);
        }
        else if (testReply->error() == 299)
        {
            processDatalessReply(RequestState::JOB_SYSTEM_DOWN);
        }
        else if (testReply->error() == 302)
        {
            processDatalessReply(RequestState::BAD_HTTP_REQUEST);
        }
        else
        {
            processDatalessReply(RequestState::GENERIC_NETWORK_ERROR);

            qCDebug(remoteInterface, "Network Error detected: %d : %s", testReply->error(), qPrintable(testReply->errorString()));
        }
        return;
    }

    QByteArray replyText = myReplyObject->readAll();

    if (myGuide->getRequestType() == AgaveRequestType::AGAVE_DOWNLOAD)
    {
        //TODO: consider a better way of doing this for larger files
        QFile * fileHandle = new QFile(taskParamList.value("localDest"));
        if (!fileHandle->open(QIODevice::WriteOnly))
        {
            fileHandle->deleteLater();
            processDatalessReply(RequestState::LOCAL_FILE_ERROR);
            return;
        }

        fileHandle->write(replyText);
        //TODO: There may be more errors here we need to catch

        fileHandle->close();
        fileHandle->deleteLater();

        emit haveDownloadReply(RequestState::GOOD, taskParamList.value("localDest"));
        return;
    }
    else if (myGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_DOWNLOAD)
    {
        //TODO: consider a better way of doing this for larger files
        emit haveBufferDownloadReply(RequestState::GOOD, replyText);

        return;
    }

    QJsonParseError parseError;
    QJsonDocument parseHandler = QJsonDocument::fromJson(replyText, &parseError);

    if (parseHandler.isNull())
    {
        processDatalessReply(RequestState::JSON_PARSE_ERROR);
        return;
    }

    qCDebug(rawHTTP, "%s",qPrintable(parseHandler.toJson()));

    RequestState prelimResult = standardSuccessFailCheck(myGuide, &parseHandler);

    if (prelimResult != RequestState::GOOD)
    {
        processDatalessReply(prelimResult);
        return;
    }

    if (myGuide->getTaskID() == "authRefresh")
    {
        processDatalessReply(RequestState::NOT_IMPLEMENTED);
    }
    else if (myGuide->getTaskID() == "dirListing")
    {
        QJsonValue expectedArray = retriveMainAgaveJSON(&parseHandler,"result");
        if (!expectedArray.isArray())
        {
            processDatalessReply(RequestState::MISSING_REPLY_DATA);
            return;
        }
        QJsonArray fileArray = expectedArray.toArray();
        QList<FileMetaData> fileList;
        for (auto itr = fileArray.constBegin(); itr != fileArray.constEnd(); itr++)
        {
            FileMetaData aFile = parseJSONfileMetaData((*itr).toObject());
            if (aFile.getFileType() == FileType::INVALID)
            {
                processDatalessReply(RequestState::MISSING_REPLY_DATA);
                return;
            }
            fileList.append(aFile);
        }
        emit haveLSReply(RequestState::GOOD, fileList);
    }
    else if ((myGuide->getTaskID() == "fileUpload") || (myGuide->getTaskID() == "filePipeUpload"))
    {
        QJsonValue expectedObject = retriveMainAgaveJSON(&parseHandler,"result");
        FileMetaData aFile = parseJSONfileMetaData(expectedObject.toObject());
        if (aFile.getFileType() == FileType::INVALID)
        {
            processDatalessReply(RequestState::MISSING_REPLY_DATA);
            return;
        }
        emit haveUploadReply(RequestState::GOOD, aFile);
    }
    else if (myGuide->getTaskID() == "fileDelete")
    {
        emit haveDeleteReply(RequestState::GOOD, taskParamList.value("toDelete"));
    }
    else if (myGuide->getTaskID() == "newFolder")
    {
        QJsonValue expectedObject = retriveMainAgaveJSON(&parseHandler,"result");
        FileMetaData aFile = parseJSONfileMetaData(expectedObject.toObject());
        if (aFile.getFileType() == FileType::INVALID)
        {
            processDatalessReply(RequestState::MISSING_REPLY_DATA);
            return;
        }
        emit haveMkdirReply(RequestState::GOOD, aFile);
    }
    else if (myGuide->getTaskID() == "renameFile")
    {
        QJsonValue expectedObject = retriveMainAgaveJSON(&parseHandler,"result");
        FileMetaData aFile = parseJSONfileMetaData(expectedObject.toObject());
        if (aFile.getFileType() == FileType::INVALID)
        {
            processDatalessReply(RequestState::MISSING_REPLY_DATA);
            return;
        }
        emit haveRenameReply(RequestState::GOOD, aFile, taskParamList.value("fullName"));
    }
    else if (myGuide->getTaskID() == "fileCopy")
    {
        QJsonValue expectedObject = retriveMainAgaveJSON(&parseHandler,"result");
        FileMetaData aFile = parseJSONfileMetaData(expectedObject.toObject());
        if (aFile.getFileType() == FileType::INVALID)
        {
            processDatalessReply(RequestState::MISSING_REPLY_DATA);
            return;
        }
        emit haveCopyReply(RequestState::GOOD, aFile);
    }
    else if (myGuide->getTaskID() == "fileMove")
    {
        QJsonValue expectedObject = retriveMainAgaveJSON(&parseHandler,"result");
        FileMetaData aFile = parseJSONfileMetaData(expectedObject.toObject());
        if (aFile.getFileType() == FileType::INVALID)
        {
            processDatalessReply(RequestState::MISSING_REPLY_DATA);
            return;
        }
        emit haveMoveReply(RequestState::GOOD, aFile, taskParamList.value("from"));
    }
    else if (myGuide->getTaskID() == "getJobList")
    {
        QJsonValue expectedObject = retriveMainAgaveJSON(&parseHandler,"result");
        QList<RemoteJobData> jobList = parseJSONjobMetaData(expectedObject.toArray());

        emit haveJobList(RequestState::GOOD, jobList);
    }
    else if (myGuide->getTaskID() == "getJobDetails")
    {
        QJsonValue expectedObject = retriveMainAgaveJSON(&parseHandler,"result");
        RemoteJobData jobData = parseJSONjobDetails(expectedObject.toObject());
        if (jobData.getState() == "ERROR")
        {
            processDatalessReply(RequestState::MISSING_REPLY_DATA);
            return;
        }
        emit haveJobDetails(RequestState::GOOD, jobData);
    }
    else if (myGuide->getTaskID() == "stopJob")
    {
        emit haveStoppedJob(RequestState::GOOD);
    }
    else if (myGuide->getTaskID() == "deleteJob")
    {
        emit haveDeletedJob(RequestState::GOOD);
    }
    else if (myGuide->getTaskID() == "getAgaveList")
    {
        //TODO More error checking here
        QJsonValue expectedArray = retriveMainAgaveJSON(&parseHandler,"result");
        QJsonArray appList = expectedArray.toArray();
        emit haveAgaveAppList(RequestState::GOOD, appList.toVariantList());
    }
    else
    {
        emit haveJobReply(RequestState::GOOD, parseHandler);
    }

}

RequestState AgaveTaskReply::standardSuccessFailCheck(AgaveTaskGuide * taskGuide, QJsonDocument * parsedDoc)
{
    //In Agave TOKEN uses a different output form
    if (taskGuide->isTokenFormat())
    {
        if (parsedDoc->object().contains("error"))
        {
            return RequestState::EXPLICIT_ERROR;
        }
    }
    else
    {
        QString statusString = retriveMainAgaveJSON(parsedDoc,"status").toString();

        if (statusString == "error")
        {
            return RequestState::EXPLICIT_ERROR;
        }
        else if (statusString != "success")
        {
            return RequestState::MISSING_REPLY_STATUS;
        }
    }
    return RequestState::GOOD;
}

FileMetaData AgaveTaskReply::parseJSONfileMetaData(QJsonObject fileNameValuePairs)
{
    FileMetaData ret;
    if (!(fileNameValuePairs.contains("format") || fileNameValuePairs.contains("nativeFormat"))
            || !fileNameValuePairs.contains("name")
            || !fileNameValuePairs.contains("path")
            || !fileNameValuePairs.value("path").isString())
    {
        return ret;
    }

    if (fileNameValuePairs.value("name").toString() == ".")
    {
        QString tmp = fileNameValuePairs.value("path").toString();
        tmp.append("/.");
        ret.setFullFilePath(tmp);
    }
    else
    {
        ret.setFullFilePath(fileNameValuePairs.value("path").toString());
    }

    QString typeString = fileNameValuePairs.value("type").toString();
    if (typeString.isEmpty())
    {
        typeString = fileNameValuePairs.value("nativeFormat").toString();
    }
    if (typeString == "dir")
    {
        ret.setType(FileType::DIR);
    }
    else if (typeString == "file")
    {
        ret.setType(FileType::FILE);
    }
    else if (typeString == "raw")
    {
        ret.setType(FileType::FILE);
    }
    //TODO: consider more validity checks here
    int fileLength = fileNameValuePairs.value("length").toInt();
    ret.setSize(fileLength);

    return ret;
}

QList<RemoteJobData> AgaveTaskReply::parseJSONjobMetaData(QJsonArray rawJobList)
{
    QList<RemoteJobData> ret;

    for (auto itr = rawJobList.constBegin(); itr != rawJobList.constEnd(); itr++)
    {
        ret.append(parseJSONjobDetails((*itr).toObject(), false));
    }

    return ret;
}

RemoteJobData AgaveTaskReply::parseJSONjobDetails(QJsonObject rawJobData, bool haveDetails)
{
    if (!rawJobData.contains("id")) return RemoteJobData::nil();
    if (!rawJobData.contains("name")) return RemoteJobData::nil();
    if (!rawJobData.contains("appId")) return RemoteJobData::nil();
    if (!rawJobData.contains("created")) return RemoteJobData::nil();
    if (!rawJobData.contains("status")) return RemoteJobData::nil();

    if (haveDetails)
    {
        if (!rawJobData.contains("inputs")) return RemoteJobData::nil();
        if (!rawJobData.contains("parameters")) return RemoteJobData::nil();
    }

    RemoteJobData ret(rawJobData.value("id").toString(),
                      rawJobData.value("name").toString(),
                      rawJobData.value("appId").toString(),
                      rawJobData.value("status").toString(),
                      parseAgaveTime(rawJobData.value("created").toString()));

    if (haveDetails)
    {
        QMap<QString, QVariant> inputMap = rawJobData.value("inputs").toObject().toVariantMap();
        QMap<QString, QVariant> paramMap = rawJobData.value("parameters").toObject().toVariantMap();
        ret.setDetails(convertVarMapToString(inputMap),convertVarMapToString(paramMap));
    }

    return ret;
}

QJsonValue AgaveTaskReply::retriveMainAgaveJSON(QJsonDocument * parsedDoc, const char * oneKey)
{
    QList<QString> smallList = { oneKey };
    return retriveMainAgaveJSON(parsedDoc, smallList);
}

QJsonValue AgaveTaskReply::retriveMainAgaveJSON(QJsonDocument * parsedDoc, QString oneKey)
{
    QList<QString> smallList = { oneKey };
    return retriveMainAgaveJSON(parsedDoc, smallList);
}

QJsonValue AgaveTaskReply::retriveMainAgaveJSON(QJsonDocument * parsedDoc, QList<QString> keyList)
{
    QJsonValue nullVal;
    if (keyList.size() < 1) return nullVal;
    if (parsedDoc->isNull()) return nullVal;
    if (parsedDoc->isEmpty()) return nullVal;
    if (!parsedDoc->isObject()) return nullVal;

    QJsonValue resultVal = recursiveJSONdig(QJsonValue(parsedDoc->object()) , &keyList, 0);
    if (resultVal.isUndefined()) return nullVal;
    if (resultVal.isNull()) return nullVal;
    return resultVal;
}

QJsonValue AgaveTaskReply::recursiveJSONdig(QJsonValue currVal, QList<QString> * keyList, int i)
{
    QJsonValue nullValue;
    if (i >= keyList->size())
    {
        return nullValue;
    }

    QString keyToFind = keyList->at(i);

    //Get next obj
    if (!currVal.isObject()) return nullValue;
    if (!currVal.toObject().contains(keyToFind)) return nullValue;
    QJsonValue targetedValue = currVal.toObject().value(keyToFind);
    if (targetedValue.isUndefined()) return nullValue;

    if (i == keyList->size() - 1)
    {
        return targetedValue;
    }

    return recursiveJSONdig(targetedValue,keyList,i+1);
}

QDateTime AgaveTaskReply::parseAgaveTime(QString agaveTime)
{
    QDateTime err; //Default obj indicates error
    //2017-03-29T15:14:00.000-05:00
    QStringList dateAndTime = agaveTime.split('T');
    if (dateAndTime.size() < 2) return err;

    QString theDate = dateAndTime.at(0);
    QString theTime = dateAndTime.at(1);

    QStringList dateParts = theDate.split('-');
    if (dateParts.size() < 3) return err;

    QStringList timeParts = theTime.split(':');
    if (timeParts.size() < 3) return err;

    QString seconds = timeParts.at(2);
    seconds.truncate(2);

    bool convOkay;
    int year = dateParts.at(0).toInt(&convOkay);
    if (!convOkay) return err;
    int mon = dateParts.at(1).toInt(&convOkay);
    if (!convOkay) return err;
    int day = dateParts.at(2).toInt(&convOkay);
    if (!convOkay) return err;

    int hour = timeParts.at(0).toInt(&convOkay);
    if (!convOkay) return err;
    int min = timeParts.at(1).toInt(&convOkay);
    if (!convOkay) return err;
    int sec = seconds.toInt(&convOkay);
    if (!convOkay) return err;

    QDate realDate(year, mon, day);
    QTime realTime(hour, min, sec);

    QDateTime ret(realDate,realTime);
    return ret;
}

QMap<QString, QString> AgaveTaskReply::convertVarMapToString(QMap<QString, QVariant> inMap)
{
    //Note: This may be very slow. If it bogs down, find a faster way.
    QMap<QString, QString> ret;
    for (auto itr = inMap.cbegin(); itr != inMap.cend(); itr++)
    {
        if (QString(itr.value().typeName()) == "QString")
        {
            ret.insert(itr.key(), itr.value().toString());
        }
        else if (QString(itr.value().typeName()) == "QVariantList")
        {
            ret.insert(itr.key(), itr.value().toStringList().at(0));
        }
    }
    return ret;
}

void AgaveTaskReply::signalConnectDelay()
{
    if (!expectsSignalConnect) return;

    //This method is a stopgap against a reply object finishing before
    //being connected to anything. This should never happen.
    int failTrys = 0;
    while (!anySignalConnect())
    {
        failTrys++;
        if (failTrys > 10)
        {
            qCDebug(remoteInterface, "ERROR: Reply object finished before/without connection to rest of program.");
            return;
        }
        QThread::usleep(10);
    }
}

bool AgaveTaskReply::anySignalConnect()
{
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::startedLogout))) return true;

    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveAuthReply))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveLSReply))) return true;

    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveDeleteReply))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveMoveReply))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveCopyReply))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveRenameReply))) return true;

    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveMkdirReply))) return true;

    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveUploadReply))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveDownloadReply))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveBufferDownloadReply))) return true;

    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveJobReply))) return true;

    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveJobList))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveJobDetails))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveStoppedJob))) return true;
    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveDeletedJob))) return true;

    if (isSignalConnected(QMetaMethod::fromSignal(&AgaveTaskReply::haveAgaveAppList))) return true;

    return false;
}
