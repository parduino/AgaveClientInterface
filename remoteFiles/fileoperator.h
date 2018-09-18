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

#ifndef FILEOPERATOR_H
#define FILEOPERATOR_H

#include <QObject>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QJsonDocument>
#include <QLoggingCategory>

#include <QFile>
#include <QDir>

#include "filenoderef.h"

Q_DECLARE_LOGGING_CATEGORY(fileManager)

class FileTreeNode;
class RemoteFileTree;
class FileMetaData;
class RemoteFileModel;
class RemoteDataInterface;
class RemoteFileItem;

enum class RequestState;
enum class NodeState;
enum class FileOperatorState {IDLE, REC_UPLOAD, REC_DOWNLOAD, REC_UPLOAD_ACTIVE, ACTIVE};
enum class RecursiveErrorCodes {NONE, MKDIR_FAIL, UPLOAD_FAIL, TYPE_MISSMATCH, LOST_FILE};
enum class RemoteDataInterfaceState;

class FileOperator : public QObject
{
    Q_OBJECT

    friend class FileTreeNode;
    friend class FileNodeRef;

public:
    FileOperator(RemoteDataInterface * theInterface, QObject *parent);
    ~FileOperator();

    void connectFileTreeWidget(RemoteFileTree * connectedWidget);

    const FileNodeRef speculateFileWithName(QString fullPath, bool folder);
    const FileNodeRef speculateFileWithName(const FileNodeRef &baseNode, QString addedPath, bool folder);

    bool operationIsPending();

    void lsClosestNode(QString fullPath, bool clearData = false);
    void lsClosestNodeToParent(QString fullPath, bool clearData = false);

    void enactRootRefresh();

    void sendDeleteReq(const FileNodeRef &selectedNode);
    void sendMoveReq(const FileNodeRef &moveFrom, QString newName);
    void sendCopyReq(const FileNodeRef &copyFrom, QString newName);
    void sendRenameReq(const FileNodeRef &selectedNode, QString newName);

    void sendCreateFolderReq(const FileNodeRef &selectedNode, QString newName);

    void sendUploadReq(const FileNodeRef &uploadTarget, QString localFile);
    void sendUploadBuffReq(const FileNodeRef &uploadTarget, QByteArray fileBuff, QString newName);
    void sendDownloadReq(const FileNodeRef &targetFile, QString localDest);
    void sendDownloadBuffReq(const FileNodeRef &targetFile);

    bool performingRecursiveDownload();
    void enactRecursiveDownload(const FileNodeRef &targetFolder, QString containingDestFolder);
    bool performingRecursiveUpload();
    void enactRecursiveUpload(const FileNodeRef &containingDestFolder, QString localFolderToCopy);
    void abortRecursiveProcess();

    void sendCompressReq(const FileNodeRef &selectedFolder);
    void sendDecompressReq(const FileNodeRef &selectedFolder);

    bool deletePopup(const FileNodeRef &toDelete);

    RemoteFileItem * getItemByFile(FileNodeRef toFind);

signals:
    //Note: it is very important that connections for these signals be queued
    void fileOpStarted();
    void fileOpDone(RequestState opState, QString err_msg);
    void fileSystemChange(FileNodeRef changedFile);

protected:
    void fileNodesChange(FileNodeRef changedFile);

    bool fileStillExtant(const FileNodeRef &theFile);
    NodeState getFileNodeState(const FileNodeRef &theFile);
    bool isAncestorOf(const FileNodeRef &parent, const FileNodeRef &child);
    const FileNodeRef getChildWithName(const FileNodeRef &baseFile, QString childName);
    const QByteArray getFileBuffer(const FileNodeRef &baseFile);
    void setFileBuffer(const FileNodeRef &theFile, const QByteArray * toSet);
    FileNodeRef getParent(const FileNodeRef &theFile);
    QList<FileNodeRef> getChildList(const FileNodeRef &theFile);
    bool nodeIsRoot(const FileNodeRef &theFile);

    void enactFolderRefresh(const FileNodeRef &selectedNode, bool clearData = false);

private slots:
    void interfaceHasNewState(RemoteDataInterfaceState newState);

    void getDeleteReply(RequestState replyState, QString toDelete);
    void getMoveReply(RequestState replyState, FileMetaData revisedFileData, QString from);
    void getCopyReply(RequestState replyState, FileMetaData newFileData);
    void getRenameReply(RequestState replyState, FileMetaData newFileData, QString oldName);

    void getMkdirReply(RequestState replyState, FileMetaData newFolderData);

    void getUploadReply(RequestState replyState, FileMetaData newFileData);
    void getDownloadReply(RequestState replyState, QString localDest);

    void getCompressReply(RequestState finalState, QJsonDocument rawData);
    void getDecompressReply(RequestState finalState, QJsonDocument rawData);

    void getRecursiveUploadReply(RequestState replyState, FileMetaData newFileData);
    void getRecursiveMkdirReply(RequestState replyState, FileMetaData newFolderData);

private:
    FileTreeNode * getFileNodeFromNodeRef(const FileNodeRef &thedata, bool verifyTimestamp = true);

    void recursiveDownloadProcessRetry();
    bool recursiveDownloadRetrivalHelper(FileTreeNode * nodeToCheck); //Return true if have all data
    bool recursiveDownloadFolderEmitHelper(QDir currentLocalDir, FileTreeNode * nodeToGet, RecursiveErrorCodes &errNum); //Return true if successful file output data
    bool emitBufferToFile(QDir containingDir, FileTreeNode * nodeToGet, RecursiveErrorCodes &errNum); //Return true if successful file output data

    void recursiveUploadProcessRetry();
    bool recursiveUploadHelper(FileTreeNode * nodeToSend, QDir localPath, RecursiveErrorCodes &errNum); //Return true if all data sent and ls verified

    void sendRecursiveCreateFolderReq(FileTreeNode * selectedNode, QString newName);
    void sendRecursiveUploadReq(FileTreeNode * uploadTarget, QString localFile);

    void emitStdFileOpErr(QString errString, RequestState errState);

    RemoteDataInterface * myInterface = nullptr;
    QString myRootFolderName;
    FileOperatorState myState = FileOperatorState::IDLE;

    FileTreeNode * rootFileNode = nullptr;

    FileTreeNode * rememberTargetFile;

    QDir recursiveLocalHead;
    FileTreeNode * recursiveRemoteHead;
    RemoteFileModel * myModel;
};

#endif // FILEOPERATOR_H
