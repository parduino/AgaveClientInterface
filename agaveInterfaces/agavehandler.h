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

#ifndef AGAVEHANDLER_H
#define AGAVEHANDLER_H

#include "remotedatainterface.h"

#include <QObject>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QFile>
#include <QBuffer>

#include <QJsonDocument>
#include <QJsonObject>

/*! \brief The AgaveRequestType is enum intended for use internal to the AgaveHandler.
 *
 *  This enum describes the various ways an http Agave request can be sent.
 */

enum class AgaveRequestType {AGAVE_GET, AGAVE_POST, AGAVE_DELETE, AGAVE_UPLOAD, AGAVE_PIPE_UPLOAD, AGAVE_PIPE_DOWNLOAD, AGAVE_DOWNLOAD, AGAVE_PUT, AGAVE_NONE, AGAVE_APP, AGAVE_JSON_POST};

class AgaveTaskGuide;
class AgaveTaskReply;

/*! \brief The AgaveHandler is a class for communicating with an Agave server over an https connection.
 *
 *  Each AgaveHandler is one use, from initialization, to login, through multiple remote requests, to logout. If an application wishes to re-login, a new AgaveHandler object should be created.
 *
 *  First, the setAgaveConnectionParams meshod should be invoked, to define some basic Agave parameters, such as the remote tenant name. Then, the performAuth method should be invoked until a successful reply is given. After that, the various remote tasks can be performed. When finished, the closeAllConnections method will logout of the remote Agave server.
 *
 */

class AgaveHandler : public RemoteDataInterface
{
    Q_OBJECT

    friend class AgaveTaskReply;

public:
    explicit AgaveHandler(QNetworkAccessManager * netAccessManager, QObject * parent = nullptr);
    ~AgaveHandler();

public slots:
    virtual QString getUserName();
    virtual RemoteDataReply * closeAllConnections();

    //Remote tasks to be implemented in subclasses:
    //Returns a RemoteDataReply, which should have the correct signal attached to an appropriate slot
    //These methods should NEVER return null
    virtual RemoteDataReply * performAuth(QString uname, QString passwd);

    virtual RemoteDataReply * remoteLS(QString dirPath);

    virtual RemoteDataReply * deleteFile(QString toDelete);
    virtual RemoteDataReply * moveFile(QString from, QString to);
    virtual RemoteDataReply * copyFile(QString from, QString to);
    virtual RemoteDataReply * renameFile(QString fullName, QString newName);

    virtual RemoteDataReply * mkRemoteDir(QString location, QString newName);

    virtual RemoteDataReply * uploadFile(QString location, QString localFileName);
    virtual RemoteDataReply * uploadBuffer(QString location, QByteArray fileData, QString newFileName);
    virtual RemoteDataReply * downloadFile(QString localDest, QString remoteName);
    virtual RemoteDataReply * downloadBuffer(QString remoteName);

    virtual RemoteDataReply * runRemoteJob(QString jobName, ParamMap jobParameters, QString remoteWorkingDir, QString indivJobName = "", QString archivePath = "");

    virtual RemoteDataReply * getListOfJobs();
    virtual RemoteDataReply * getJobDetails(QString IDstr);
    virtual RemoteDataReply * stopJob(QString IDstr);
    virtual RemoteDataReply * deleteJob(QString IDstr);

    virtual RemoteDataInterfaceState getInterfaceState();

    //On Agave Apps:
    //Register info on the Agave App's parameters, using:
    void registerAgaveAppInfo(QString agaveAppName, QString fullAgaveName, QStringList parameterList, QStringList inputList, QString workingDirParameter);
    //After that, use the standard runRemoteJob, where jobName is the agaveAppName,
    //the job parameters are a list matching the inputs/parameters given by parameterList and inputList
    //and the remoteWorkingDir will be used as a input/parameter named in remoteDirParameter (optional)

    //For debugging purposes, to retrive the list of available Agave Apps:
    AgaveTaskReply *getAgaveAppList();

    void setAgaveConnectionParams(QString tenant, QString clientId, QString storage);

    RemoteDataReply * runAgaveJob(QJsonDocument rawJobJSON);

protected:
    void handleInternalTask(AgaveTaskReply *agaveReply, QNetworkReply * rawReply);
    void handleInternalTask(AgaveTaskReply *agaveReply, RequestState taskState);

private slots:
    void finishedOneTask();

private:
    AgaveTaskReply * performAgaveQuery(QString queryName);
    AgaveTaskReply * performAgaveQuery(QString queryName, QMap<QString, QByteArray> varList, AgaveTaskReply *parentReq = nullptr);
    AgaveTaskReply * createDirectReply(AgaveTaskGuide * theTaskType, RequestState errorState, AgaveTaskReply *parentReq = nullptr);
    AgaveTaskReply * createDirectReply(QString theTaskType, RequestState errorState, AgaveTaskReply *parentReq = nullptr);

    QNetworkReply * distillRequestData(AgaveTaskGuide * theGuide, QMap<QString, QByteArray> * varList);
    QNetworkReply * finalizeAgaveRequest(AgaveTaskGuide * theGuide, QString urlAppend, QByteArray * authHeader = nullptr, QByteArray postData = "", QIODevice * fileHandle = nullptr);

    void forwardReplyToParent(AgaveTaskReply * agaveReply, RequestState replyState);

    bool noPendingHttpRequests();
    void changeAuthState(RemoteDataInterfaceState newState);

    void setupTaskGuideList();
    void insertAgaveTaskGuide(AgaveTaskGuide * newGuide);
    AgaveTaskGuide * retriveTaskGuide(QString taskID);

    static bool remotePathStringIsValid(QString toCheck);

    QNetworkAccessManager * networkHandle;
    QSslConfiguration SSLoptions;

    QString tenantURL;
    QString clientName;
    QString storageNode;

    QByteArray authEncoded;
    QByteArray clientEncoded;
    QByteArray token;
    QByteArray tokenHeader;
    QByteArray refreshToken;

    QString authUname;
    QString authPass;
    QString clientKey;
    QString clientSecret;

    QMap<QString, AgaveTaskGuide*> validTaskList;

    QString pwd = "";

    int pendingRequestCount = 0;
    RemoteDataInterfaceState currentState = RemoteDataInterfaceState::INIT;
};

#endif // AGAVEHANDLER_H
