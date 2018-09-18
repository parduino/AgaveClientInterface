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

#include "fileoperator.h"

#include "remotefiletree.h"
#include "filetreenode.h"
#include "filenoderef.h"
#include "remotefilemodel.h"

#include "filemetadata.h"
#include "remotedatainterface.h"

Q_LOGGING_CATEGORY(fileManager, "File Manager")

FileOperator::FileOperator(RemoteDataInterface * theInterface, QObject *parent) : QObject(parent)
{
    myInterface = theInterface;
    if (myInterface == nullptr)
    {
        qFatal("Cannot create JobOperator object with null remote interface.");
    }
    myModel = new RemoteFileModel(this);
    QObject::connect(this, SIGNAL(fileSystemChange(FileNodeRef)), myModel, SLOT(newFileData(FileNodeRef)), Qt::QueuedConnection);

    QObject::connect(myInterface, SIGNAL(connectionStateChanged(RemoteDataInterfaceState)), this, SLOT(interfaceHasNewState(RemoteDataInterfaceState)));
}

FileOperator::~FileOperator()
{
    delete rootFileNode;
}

void FileOperator::connectFileTreeWidget(RemoteFileTree * connectedWidget)
{
    connectedWidget->setModel(myModel->getRawModel());
}

FileTreeNode * FileOperator::getFileNodeFromNodeRef(const FileNodeRef &thedata, bool verifyTimestamp)
{
    if (thedata.isNil()) return nullptr;
    FileTreeNode * ret = rootFileNode->getNodeWithName(thedata.getFullPath());
    if (ret == nullptr) return nullptr;

    if (!verifyTimestamp) return ret;

    if (ret->getFileData().getTimestamp() != thedata.getTimestamp()) return nullptr;
    return ret;
}

void FileOperator::enactRootRefresh()
{
    qCDebug(fileManager, "Enacting refresh of root.");
    QString rootFolder = "/";
    rootFolder = rootFolder.append(myRootFolderName);
    RemoteDataReply * theReply = myInterface->remoteLS(rootFolder);

    rootFileNode->setLStask(theReply);
}

void FileOperator::enactFolderRefresh(const FileNodeRef &selectedNode, bool clearData)
{
    FileTreeNode * trueNode = getFileNodeFromNodeRef(selectedNode);
    if (trueNode == nullptr) return;
    if (clearData)
    {
        trueNode->deleteFolderContentsData();
    }

    if (trueNode->haveLStask())
    {
        return;
    }
    QString fullFilePath = trueNode->getFileData().getFullPath();

    qCDebug(fileManager, "File Path Needs refresh: %s", qPrintable(fullFilePath));
    RemoteDataReply * theReply = myInterface->remoteLS(fullFilePath);

    trueNode->setLStask(theReply);
}

bool FileOperator::operationIsPending()
{
    return (myState != FileOperatorState::IDLE);
}

void FileOperator::interfaceHasNewState(RemoteDataInterfaceState newState)
{
    if (newState != RemoteDataInterfaceState::CONNECTED) return;

    myRootFolderName =  myInterface->getUserName();
    rootFileNode = new FileTreeNode(myRootFolderName, this);

    enactRootRefresh();
}

void FileOperator::sendDeleteReq(const FileNodeRef &selectedNode)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!selectedNode.fileNodeExtant()) return;

    QString targetFile = selectedNode.getFullPath();
    qCDebug(fileManager, "Starting delete procedure: %s",qPrintable(targetFile));
    RemoteDataReply * theReply = myInterface->deleteFile(targetFile);

    QObject::connect(theReply, SIGNAL(haveDeleteReply(RequestState, QString)),
                     this, SLOT(getDeleteReply(RequestState, QString)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::getDeleteReply(RequestState replyState, QString toDelete)
{
    myState = FileOperatorState::IDLE;

    if (replyState == RequestState::GOOD)
    {
        lsClosestNodeToParent(toDelete);
        emit fileOpDone(replyState, QString("File successfully deleted: %1").arg(toDelete));
    }
    else
    {
        emit fileOpDone(replyState, QString("Unable to delete file: %1").arg(RemoteDataInterface::interpretRequestState(replyState)));
    }
}

void FileOperator::sendMoveReq(const FileNodeRef &moveFrom, QString newName)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!moveFrom.fileNodeExtant()) return;

    qCDebug(fileManager, "Starting move procedure: %s to %s",
            qPrintable(moveFrom.getFullPath()),
            qPrintable(newName));
    RemoteDataReply * theReply = myInterface->moveFile(moveFrom.getFullPath(), newName);

    QObject::connect(theReply, SIGNAL(haveMoveReply(RequestState,FileMetaData, QString)),
                     this, SLOT(getMoveReply(RequestState,FileMetaData, QString)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::getMoveReply(RequestState replyState, FileMetaData revisedFileData, QString from)
{
    myState = FileOperatorState::IDLE;

    if (replyState == RequestState::GOOD)
    {
        lsClosestNodeToParent(from);
        lsClosestNode(revisedFileData.getFullPath());
        emit fileOpDone(replyState, QString("File successfully moved from: %1 to: %2")
                        .arg(from)
                        .arg(revisedFileData.getFullPath()));
    }
    else
    {
        emitStdFileOpErr("Unable to move file", replyState);
    }
}

void FileOperator::sendCopyReq(const FileNodeRef &copyFrom, QString newName)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!copyFrom.fileNodeExtant()) return;

    qCDebug(fileManager, "Starting copy procedure: %s to %s",
           qPrintable(copyFrom.getFullPath()),
           qPrintable(newName));
    RemoteDataReply * theReply = myInterface->copyFile(copyFrom.getFullPath(), newName);

    QObject::connect(theReply, SIGNAL(haveCopyReply(RequestState,FileMetaData)),
                     this, SLOT(getCopyReply(RequestState,FileMetaData)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::getCopyReply(RequestState replyState, FileMetaData newFileData)
{
    myState = FileOperatorState::IDLE;

    if (replyState == RequestState::GOOD)
    {
        lsClosestNode(newFileData.getFullPath());
        emit fileOpDone(replyState, QString("File successfully copied: %1").arg(newFileData.getFullPath()));
    }
    else
    {
        emitStdFileOpErr("Unable to copy file", replyState);
    }
}

void FileOperator::sendRenameReq(const FileNodeRef &selectedNode, QString newName)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!selectedNode.fileNodeExtant()) return;

    qCDebug(fileManager, "Starting rename procedure: %s to %s",
           qPrintable(selectedNode.getFullPath()),
           qPrintable(newName));
    RemoteDataReply * theReply = myInterface->renameFile(selectedNode.getFullPath(), newName);

    QObject::connect(theReply, SIGNAL(haveRenameReply(RequestState,FileMetaData, QString)),
                     this, SLOT(getRenameReply(RequestState,FileMetaData, QString)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::getRenameReply(RequestState replyState, FileMetaData newFileData, QString oldName)
{
    myState = FileOperatorState::IDLE;

    if (replyState == RequestState::GOOD)
    {
        lsClosestNodeToParent(oldName);
        lsClosestNodeToParent(newFileData.getFullPath());
        emit fileOpDone(replyState, QString("File successfully renamed from %1 to %2")
                        .arg(oldName)
                        .arg(newFileData.getFullPath()));
    }
    else
    {
        emitStdFileOpErr("Unable to rename file", replyState);
    }
}

void FileOperator::sendCreateFolderReq(const FileNodeRef &selectedNode, QString newName)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!selectedNode.fileNodeExtant()) return;

    qCDebug(fileManager,"Starting create folder procedure: %s at %s",
           qPrintable(selectedNode.getFullPath()),
           qPrintable(newName));
    RemoteDataReply * theReply = myInterface->mkRemoteDir(selectedNode.getFullPath(), newName);

    QObject::connect(theReply, SIGNAL(haveMkdirReply(RequestState,FileMetaData)),
                     this, SLOT(getMkdirReply(RequestState,FileMetaData)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::getMkdirReply(RequestState replyState, FileMetaData newFolderData)
{
    myState = FileOperatorState::IDLE;

    if (replyState == RequestState::GOOD)
    {
        lsClosestNode(newFolderData.getContainingPath());
        emit fileOpDone(replyState, QString("New Folder Created at %1")
                        .arg(newFolderData.getFullPath()));
    }
    else
    {
        emitStdFileOpErr("Unable to create remote folder", replyState);
    }
}

void FileOperator::sendUploadReq(const FileNodeRef &uploadTarget, QString localFile)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!uploadTarget.fileNodeExtant()) return;

    qCDebug(fileManager, "Starting upload procedure: %s to %s", qPrintable(localFile),
           qPrintable(uploadTarget.getFullPath()));
    RemoteDataReply * theReply = myInterface->uploadFile(uploadTarget.getFullPath(), localFile);

    QObject::connect(theReply, SIGNAL(haveUploadReply(RequestState,FileMetaData)),
                     this, SLOT(getUploadReply(RequestState,FileMetaData)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::sendUploadBuffReq(const FileNodeRef &uploadTarget, QByteArray fileBuff, QString newName)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!uploadTarget.fileNodeExtant()) return;

    qCDebug(fileManager, "Starting upload procedure: to %s", qPrintable(uploadTarget.getFullPath()));
    RemoteDataReply * theReply = myInterface->uploadBuffer(uploadTarget.getFullPath(), fileBuff, newName);

    QObject::connect(theReply, SIGNAL(haveUploadReply(RequestState,FileMetaData)),
                     this, SLOT(getUploadReply(RequestState,FileMetaData)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::getUploadReply(RequestState replyState, FileMetaData newFileData)
{
    myState = FileOperatorState::IDLE;

    if (replyState == RequestState::GOOD)
    {
        lsClosestNodeToParent(newFileData.getFullPath());
        emit fileOpDone(replyState, QString("File successfully uploaded to %1")
                        .arg(newFileData.getFullPath()));
    }
    else
    {
        emitStdFileOpErr("Unable to upload file", replyState);
    }
}

void FileOperator::sendDownloadReq(const FileNodeRef &targetFile, QString localDest)
{   
    if (myState != FileOperatorState::IDLE) return;
    if (!targetFile.fileNodeExtant()) return;

    qCDebug(fileManager, "Starting download procedure: %s to %s", qPrintable(targetFile.getFullPath()),
           qPrintable(localDest));
    RemoteDataReply * theReply = myInterface->downloadFile(localDest, targetFile.getFullPath());

    QObject::connect(theReply, SIGNAL(haveDownloadReply(RequestState, QString)),
                     this, SLOT(getDownloadReply(RequestState, QString)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::sendDownloadBuffReq(const FileNodeRef &targetFile)
{
    if (!targetFile.fileNodeExtant()) return;
    FileTreeNode * trueNode = getFileNodeFromNodeRef(targetFile);
    if (trueNode->haveBuffTask())
    {
        return;
    }
    qCDebug(fileManager, "Starting download buffer procedure: %s", qPrintable(targetFile.getFullPath()));
    RemoteDataReply * theReply = myInterface->downloadBuffer(targetFile.getFullPath());
    trueNode->setBuffTask(theReply);
}

void FileOperator::getDownloadReply(RequestState replyState, QString localDest)
{
    myState = FileOperatorState::IDLE;

    if (replyState == RequestState::GOOD)
    {
        emit fileOpDone(replyState, QString("Download complete to %1")
                        .arg(localDest));
    }
    else
    {
        emitStdFileOpErr("Unable to download requested file", replyState);
    }
}

bool FileOperator::performingRecursiveDownload()
{
    return (myState == FileOperatorState::REC_DOWNLOAD);
}

void FileOperator::enactRecursiveDownload(const FileNodeRef &targetFolder, QString containingDestFolder)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!targetFolder.fileNodeExtant()) return;

    if (targetFolder.getFileType() != FileType::DIR)
    {
        emit fileOpDone(RequestState::INVALID_PARAM, "ERROR: Only folders can be downloaded recursively.");
        return;
    }

    QDir downloadParent(containingDestFolder);

    if (!downloadParent.exists())
    {
        emit fileOpDone(RequestState::LOCAL_FILE_ERROR, "ERROR: Download destination does not exist.");
        return;
    }

    if (downloadParent.exists(targetFolder.getFileName()))
    {
        emit fileOpDone(RequestState::LOCAL_FILE_ERROR, "ERROR: Download destination already occupied.");
        return;
    }

    if (!downloadParent.mkdir(targetFolder.getFileName()))
    {
        emit fileOpDone(RequestState::LOCAL_FILE_ERROR, "ERROR: Unable to create local destination for download, please check that you have permissions to write to the specified folder.");
        return;
    }
    recursiveLocalHead = downloadParent;
    if (!recursiveLocalHead.cd(targetFolder.getFileName()))
    {
        emit fileOpDone(RequestState::LOCAL_FILE_ERROR, "ERROR: Unable to create local destination for download, please check that you have permissions to write to the specified folder.");
        return;
    }

    recursiveRemoteHead = getFileNodeFromNodeRef(targetFolder);
    myState = FileOperatorState::REC_DOWNLOAD;
    emit fileOpStarted();
    recursiveDownloadProcessRetry();
}

bool FileOperator::performingRecursiveUpload()
{
    return (myState == FileOperatorState::REC_UPLOAD) || (myState == FileOperatorState::REC_UPLOAD_ACTIVE);
}

void FileOperator::enactRecursiveUpload(const FileNodeRef &containingDestFolder, QString localFolderToCopy)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!containingDestFolder.fileNodeExtant()) return;

    recursiveLocalHead = QDir(localFolderToCopy);
    if (!recursiveLocalHead.exists())
    {
        fileOpDone(RequestState::INVALID_PARAM, "ERROR: The folder to upload does not exist.");
        return;
    }

    if (!recursiveLocalHead.isReadable())
    {
        fileOpDone(RequestState::LOCAL_FILE_ERROR, "ERROR: Unable to read from local folder to upload, please check that you have permissions to read the specified folder.");
        return;
    }

    if (recursiveLocalHead.dirName().isEmpty())
    {
        fileOpDone(RequestState::INVALID_PARAM, "ERROR: Cannot upload unnamed or root folders.");
        return;
    }

    if (containingDestFolder.getFileType() != FileType::DIR)
    {
        fileOpDone(RequestState::INVALID_PARAM, "ERROR: The destination for an upload must be a folder.");
        return;
    }

    if (!containingDestFolder.folderContentsLoaded())
    {
        fileOpDone(RequestState::INVALID_PARAM, "ERROR: The destination for an upload must be fully loaded.");
        return;
    }

    if (!containingDestFolder.getChildWithName(recursiveLocalHead.dirName()).isNil())
    {
        fileOpDone(RequestState::INVALID_PARAM, "ERROR: The destination for the upload is already occupied.");
        return;
    }

    recursiveRemoteHead = getFileNodeFromNodeRef(containingDestFolder);
    myState = FileOperatorState::REC_UPLOAD;
    emit fileOpStarted();
    recursiveUploadProcessRetry();
}

void FileOperator::abortRecursiveProcess()
{
    QString toDisplay = "Internal ERROR";

    if (performingRecursiveDownload())
    {
        toDisplay = "Folder download stopped by user.";
    }
    else if (performingRecursiveUpload())
    {
        toDisplay = "Folder upload stopped by user.";
    }
    else
    {
        return;
    }

    if (myState == FileOperatorState::REC_UPLOAD_ACTIVE)
    {
        myState = FileOperatorState::ACTIVE;
    }
    else
    {
        myState = FileOperatorState::IDLE;
    }

    fileOpDone(RequestState::STOPPED_BY_USER, toDisplay);
    return;
}

void FileOperator::sendCompressReq(const FileNodeRef &selectedFolder)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!selectedFolder.fileNodeExtant()) return;
    qCDebug(fileManager, "Folder compress specified");
    QMultiMap<QString, QString> oneInput;
    oneInput.insert("compression_type","tgz");

    if (selectedFolder.getFileType() != FileType::DIR)
    {
        //TODO: give reasonable error
        return;
    }
    RemoteDataReply * compressTask = myInterface->runRemoteJob("compress",oneInput,selectedFolder.getFullPath());
    QObject::connect(compressTask, SIGNAL(haveJobReply(RequestState,QJsonDocument)),
                     this, SLOT(getCompressReply(RequestState,QJsonDocument)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::getCompressReply(RequestState finalState, QJsonDocument)
{
    myState = FileOperatorState::IDLE;

    //TODO: ask for refresh of relevant containing folder, after finishing job
    emit fileOpDone(finalState, "compress enacted");

    if (finalState != RequestState::GOOD)
    {
        //TODO: give reasonable error
    }
}

void FileOperator::sendDecompressReq(const FileNodeRef &selectedFolder)
{
    if (myState != FileOperatorState::IDLE) return;
    if (!selectedFolder.fileNodeExtant()) return;
    qCDebug(fileManager, "Folder de-compress specified");
    QMultiMap<QString, QString> oneInput;

    if (selectedFolder.getFileType() == FileType::DIR)
    {
        //TODO: give reasonable error
        return;
    }
    oneInput.insert("inputFile",selectedFolder.getFullPath());

    RemoteDataReply * decompressTask = myInterface->runRemoteJob("extract",oneInput,"");
    QObject::connect(decompressTask, SIGNAL(haveJobReply(RequestState,QJsonDocument)),
                     this, SLOT(getDecompressReply(RequestState,QJsonDocument)));
    myState = FileOperatorState::ACTIVE;
    emit fileOpStarted();
}

void FileOperator::getDecompressReply(RequestState finalState, QJsonDocument)
{
    myState = FileOperatorState::IDLE;

    //TODO: ask for refresh of relevant containing folder, after finishing job
    emit fileOpDone(finalState, "deconpress enacted");

    if (finalState != RequestState::GOOD)
    {
        //TODO: give reasonable error
    }
}

void FileOperator::fileNodesChange(FileNodeRef changedFile)
{
    emit fileSystemChange(changedFile);

    if (performingRecursiveDownload())
    {
        recursiveDownloadProcessRetry();
    }
    else if (performingRecursiveUpload())
    {
        recursiveUploadProcessRetry();
    }
}

void FileOperator::getRecursiveUploadReply(RequestState replyState, FileMetaData newFileData)
{
    if (myState != FileOperatorState::REC_UPLOAD_ACTIVE)
    {
        myState = FileOperatorState::IDLE;
        return;
    }
    myState = FileOperatorState::REC_UPLOAD;

    if (replyState != RequestState::GOOD)
    {
        myState = FileOperatorState::IDLE;
        emitStdFileOpErr("Folder upload failed to upload file", replyState);
        return;
    }
    lsClosestNodeToParent(newFileData.getFullPath());
}

void FileOperator::getRecursiveMkdirReply(RequestState replyState, FileMetaData newFolderData)
{
    if (myState != FileOperatorState::REC_UPLOAD_ACTIVE)
    {
        myState = FileOperatorState::IDLE;
        return;
    }
    myState = FileOperatorState::REC_UPLOAD;

    if (replyState != RequestState::GOOD)
    {
        myState = FileOperatorState::IDLE;
        emitStdFileOpErr("Folder upload failed to create new remote folder", replyState);
        return;
    }
    lsClosestNode(newFolderData.getContainingPath());
}

void FileOperator::lsClosestNode(QString fullPath, bool clearData)
{
    FileTreeNode * nodeToRefresh = rootFileNode->getClosestNodeWithName(fullPath);
    enactFolderRefresh(nodeToRefresh->getFileData(), clearData);
}

void FileOperator::lsClosestNodeToParent(QString fullPath, bool clearData)
{
    FileTreeNode * nodeToRefresh = rootFileNode->getNodeWithName(fullPath);
    if (nodeToRefresh != nullptr)
    {
        if (!nodeToRefresh->isRootNode())
        {
            nodeToRefresh = nodeToRefresh->getParentNode();
        }
        enactFolderRefresh(nodeToRefresh->getFileData(), clearData);
        return;
    }

    nodeToRefresh = rootFileNode->getClosestNodeWithName(fullPath);
    enactFolderRefresh(nodeToRefresh->getFileData());
}

bool FileOperator::fileStillExtant(const FileNodeRef &theFile)
{
    FileTreeNode * scanNode = getFileNodeFromNodeRef(theFile);
    return (scanNode != nullptr);
}

NodeState FileOperator::getFileNodeState(const FileNodeRef &theFile)
{
    FileTreeNode * scanNode = getFileNodeFromNodeRef(theFile);
    if (scanNode == nullptr) return NodeState::NON_EXTANT;
    return scanNode->getNodeState();
}

bool FileOperator::isAncestorOf(const FileNodeRef &parent, const FileNodeRef &child)
{
    //Also returns false if one or the other is not extant
    FileTreeNode *  parentNode = getFileNodeFromNodeRef(parent);
    if (parentNode == nullptr) return false;
    FileTreeNode *  childNode = getFileNodeFromNodeRef(child);
    if (childNode == nullptr) return false;

    return childNode->isChildOf(parentNode);
}

const FileNodeRef FileOperator::speculateFileWithName(QString fullPath, bool folder)
{
    FileTreeNode * scanNode = rootFileNode->getNodeWithName(fullPath);
    if (scanNode != nullptr)
    {
        return scanNode->getFileData();
    }
    scanNode = rootFileNode->getClosestNodeWithName(fullPath);
    if (scanNode == nullptr)
    {
        return FileNodeRef::nil();
    }
    QStringList fullPathParts = FileMetaData::getPathNameList(fullPath);
    QStringList scanPathParts = FileMetaData::getPathNameList(scanNode->getFileData().getFullPath());

    int accountedParts = scanPathParts.size();
    QString pathSoFar = "";

    for (auto itr = fullPathParts.cbegin(); itr != fullPathParts.cend(); itr++)
    {
        if (accountedParts <= 0)
        {
            pathSoFar = pathSoFar.append("/");
            pathSoFar = pathSoFar.append(*itr);
        }
        else
        {
            accountedParts--;
        }
    }
    return speculateFileWithName(scanNode->getFileData(), pathSoFar, folder);
}

const FileNodeRef FileOperator::speculateFileWithName(const FileNodeRef &baseNode, QString addedPath, bool folder)
{
    FileTreeNode * searchNode = getFileNodeFromNodeRef(baseNode);
    QStringList pathParts = FileMetaData::getPathNameList(addedPath);
    for (auto itr = pathParts.cbegin(); itr != pathParts.cend(); itr++)
    {
        FileTreeNode * nextNode = searchNode->getChildNodeWithName(*itr);
        if (nextNode != nullptr)
        {
            searchNode = nextNode;
            continue;
        }
        if (!searchNode->isFolder())
        {
            qCDebug(fileManager, "Invalid file speculation path.");
            return FileNodeRef::nil();
        }
        if (searchNode->getNodeState() == NodeState::FOLDER_CONTENTS_LOADED)
        {
            //Speculation failed, file known to not exist
            return FileNodeRef::nil();
        }

        FileMetaData newFolderData;
        QString newPath = searchNode->getFileData().getFullPath();
        newPath = newPath.append("/");
        newPath = newPath.append(*itr);
        newFolderData.setFullFilePath(newPath);
        newFolderData.setType(FileType::DIR);
        if ((itr + 1 == pathParts.cend()) && (folder == false))
        {
            newFolderData.setType(FileType::FILE);
        }
        newFolderData.setSize(0);
        nextNode = new FileTreeNode(newFolderData, searchNode);
        enactFolderRefresh(searchNode->getFileData());

        searchNode = nextNode;
    }

    if (folder)
    {
        if (searchNode->getNodeState() != NodeState::FOLDER_CONTENTS_LOADED)
        {
            enactFolderRefresh(searchNode->getFileData());
        }
    }
    else if (searchNode->getFileBuffer() == nullptr)
    {
        sendDownloadBuffReq(searchNode->getFileData());
    }

    return searchNode->getFileData();
}

const FileNodeRef FileOperator::getChildWithName(const FileNodeRef &baseFile, QString childName)
{
    FileTreeNode * baseNode = getFileNodeFromNodeRef(baseFile);
    if (baseNode == nullptr) return FileNodeRef::nil(); //TODO: Consider exception handling here
    FileTreeNode * childNode = baseNode->getChildNodeWithName(childName);
    if (childNode == nullptr) return FileNodeRef::nil();
    return childNode->getFileData();
}

const QByteArray FileOperator::getFileBuffer(const FileNodeRef &baseFile)
{
    FileTreeNode * baseNode = getFileNodeFromNodeRef(baseFile);
    if (baseNode == nullptr) return nullptr; //TODO: Consider exception handling here
    QByteArray ret;
    QByteArray * storedArray = baseNode->getFileBuffer();
    if (storedArray == nullptr) return ret;
    return *storedArray;
}

void FileOperator::setFileBuffer(const FileNodeRef &theFile, const QByteArray * toSet)
{
    FileTreeNode * baseNode = getFileNodeFromNodeRef(theFile);
    if (baseNode == nullptr) return; //TODO: Consider exception handling here
    baseNode->setFileBuffer(toSet);
}

FileNodeRef FileOperator::getParent(const FileNodeRef &theFile)
{
    FileTreeNode * baseNode = getFileNodeFromNodeRef(theFile);
    if (baseNode == nullptr) return FileNodeRef::nil();
    FileTreeNode * parentNode = baseNode->getParentNode();
    if (parentNode == nullptr) return FileNodeRef::nil();
    return parentNode->getFileData();
}

QList<FileNodeRef> FileOperator::getChildList(const FileNodeRef &theFile)
{
    QList<FileNodeRef> ret;
    FileTreeNode * baseNode = getFileNodeFromNodeRef(theFile);
    if (baseNode == nullptr) return ret; //TODO: Consider exception handling here
    for (FileTreeNode * aNode : baseNode->getChildList())
    {
        ret.append(aNode->getFileData());
    }
    return ret;
}

bool FileOperator::nodeIsRoot(const FileNodeRef &theFile)
{
    FileTreeNode * theNode = getFileNodeFromNodeRef(theFile);
    if (theNode == nullptr) return false;
    return theNode->isRootNode();
}

bool FileOperator::deletePopup(const FileNodeRef &toDelete)
{
    QMessageBox deleteQuery;
    deleteQuery.setText(QString("Are you sure you wish to delete the file:\n\n%1").arg(toDelete.getFullPath()));
    deleteQuery.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    deleteQuery.setDefaultButton(QMessageBox::Cancel);
    int button = deleteQuery.exec();
    switch (button)
    {
      case QMessageBox::Yes:
          return true;
      default:
          return false;
    }
    return false;
}

RemoteFileItem * FileOperator::getItemByFile(FileNodeRef toFind)
{
    return myModel->getItemByFile(toFind);
}

void FileOperator::recursiveDownloadProcessRetry()
{
    if (myState != FileOperatorState::REC_DOWNLOAD) return;

    if (recursiveDownloadRetrivalHelper(recursiveRemoteHead))
    {
        QString outText = "INTERNAL ERROR";
        RecursiveErrorCodes errNum = RecursiveErrorCodes::NONE;
        bool success = recursiveDownloadFolderEmitHelper(recursiveLocalHead, recursiveRemoteHead, errNum);
        if (success)
        {
            myState = FileOperatorState::IDLE;
            emit fileOpDone(RequestState::GOOD,"Remote folder downloaded");
            return;
        }

        if (errNum == RecursiveErrorCodes::LOST_FILE)
        {
            outText = "Internal Error: File entry missing in downloaded data. Files may have changed outside of program.";
        }
        else if (errNum == RecursiveErrorCodes::TYPE_MISSMATCH)
        {
            outText = "Internal Error: Type Mismatch in downloaded data. Files may have changed outside of program.";
        }
        else
        {
            outText = "Unable to write local files for download, please check that you have permissions to write to the specified folder.";
        }

        myState = FileOperatorState::IDLE;
        emit fileOpDone(RequestState::UNCLASSIFIED, outText);
        return;
    }
}

bool FileOperator::recursiveDownloadRetrivalHelper(FileTreeNode * nodeToCheck)
{
    if (nodeToCheck->isFile())
    {
        if (nodeToCheck->getFileBuffer() == nullptr)
        {
            sendDownloadBuffReq(nodeToCheck->getFileData());
            return false;
        }
        return true;
    }

    if (!nodeToCheck->isFolder()) return true; //For now, we only copy files and folders

    bool foundAll = true;

    if (nodeToCheck->getNodeState() != NodeState::FOLDER_CONTENTS_LOADED)
    {
        foundAll = false;
        enactFolderRefresh(nodeToCheck->getFileData());
    }

    for (FileTreeNode * aChild : nodeToCheck->getChildList())
    {
        if (!recursiveDownloadRetrivalHelper(aChild))
        {
            foundAll = false;
        }
    }

    return foundAll;
}

bool FileOperator::recursiveDownloadFolderEmitHelper(QDir currentLocalDir, FileTreeNode *nodeToGet, RecursiveErrorCodes &errNum)
{
    if (!nodeToGet->isFolder())
    {
        errNum = RecursiveErrorCodes::TYPE_MISSMATCH;
        return false;
    }
    if (!currentLocalDir.exists())
    {
        errNum = RecursiveErrorCodes::LOST_FILE;
        return false;
    }
    for (FileTreeNode * aChild : nodeToGet->getChildList())
    {
        if (aChild->getFileData().getFileType() == FileType::DIR)
        {
            if (!currentLocalDir.mkdir(aChild->getFileData().getFileName())) return false;
            QDir newFolder = currentLocalDir;
            newFolder.cd(aChild->getFileData().getFileName());
            if (!newFolder.exists()) return false;
            if (!recursiveDownloadFolderEmitHelper(newFolder, aChild, errNum)) return false;
        }
        else if (aChild->getFileData().getFileType() == FileType::FILE)
        {
            if (!emitBufferToFile(currentLocalDir, aChild, errNum)) return false;
        }
    }

    return true;
}

bool FileOperator::emitBufferToFile(QDir containingDir, FileTreeNode * nodeToGet, RecursiveErrorCodes &errNum)
{
    if (!nodeToGet->isFile())
    {
        errNum = RecursiveErrorCodes::TYPE_MISSMATCH;
        return false;
    }

    if (!containingDir.exists())
    {
        errNum = RecursiveErrorCodes::LOST_FILE;
        return false;
    }

    if (containingDir.exists(nodeToGet->getFileData().getFileName())) return false;

    QFile newFile(containingDir.absoluteFilePath(nodeToGet->getFileData().getFileName()));
    if (nodeToGet->getFileBuffer() == nullptr) return false;
    if (!newFile.open(QFile::WriteOnly)) return false;
    if (newFile.write(*(nodeToGet->getFileBuffer())) < 0) return false;
    newFile.close();

    return true;
}

void FileOperator::recursiveUploadProcessRetry()
{
    if (myState != FileOperatorState::REC_UPLOAD) return;

    RecursiveErrorCodes theError = RecursiveErrorCodes::NONE;
    FileTreeNode * trueRemoteHead = recursiveRemoteHead->getChildNodeWithName(recursiveLocalHead.dirName());
    if (trueRemoteHead == nullptr)
    {
        sendRecursiveCreateFolderReq(recursiveRemoteHead, recursiveLocalHead.dirName());
        return;
    }

    if (recursiveUploadHelper(trueRemoteHead, recursiveLocalHead, theError))
    {
        myState = FileOperatorState::IDLE;
        emit fileOpDone(RequestState::GOOD, "Folder uploaded.");
        return;
    }

    if (theError == RecursiveErrorCodes::NONE) return;

    myState = FileOperatorState::IDLE;
    if (theError == RecursiveErrorCodes::MKDIR_FAIL)
    {
        emit fileOpDone(RequestState::UNCLASSIFIED, "Create folder operation failed during recursive upload. Check your network connection and try again.");
        return;
    }

    if (theError == RecursiveErrorCodes::UPLOAD_FAIL)
    {
        emit fileOpDone(RequestState::UNCLASSIFIED, "File upload operation failed during recursive upload. Check your network connection and try again.");
        return;
    }

    if (theError == RecursiveErrorCodes::TYPE_MISSMATCH)
    {
        emit fileOpDone(RequestState::UNCLASSIFIED, "Internal error. File type mismatch. Remote files may be being accessed outside of this program.");
        return;
    }
}

bool FileOperator::recursiveUploadHelper(FileTreeNode * nodeToSend, QDir localPath, RecursiveErrorCodes &errNum)
{
    errNum = RecursiveErrorCodes::NONE;

    if (!nodeToSend->isFolder())
    {
        errNum = RecursiveErrorCodes::TYPE_MISSMATCH;
        return false;
    }

    if (nodeToSend->getNodeState() != NodeState::FOLDER_CONTENTS_LOADED)
    {
        enactFolderRefresh(nodeToSend->getFileData());
        return false;
    }

    for (QFileInfo anEntry : localPath.entryInfoList(QDir::Dirs	| QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot))
    {
        if (anEntry.isDir())
        {
            QDir childDir = anEntry.dir();
            childDir.cd(anEntry.fileName());
            FileTreeNode * childNode = nodeToSend->getChildNodeWithName(childDir.dirName());
            if (childNode == nullptr)
            {
                sendRecursiveCreateFolderReq(nodeToSend, childDir.dirName());
                return false;
            }
            if (!recursiveUploadHelper(childNode, childDir, errNum)) return false;
        }
        else if (anEntry.isFile())
        {
            FileTreeNode * childNode = nodeToSend->getChildNodeWithName(anEntry.fileName());
            if (childNode == nullptr)
            {
                sendRecursiveUploadReq(nodeToSend, anEntry.absoluteFilePath());
                return false;
            }
            if (!childNode->isFile())
            {
                errNum = RecursiveErrorCodes::TYPE_MISSMATCH;
                return false;
            }
        }
    }

    return true;
}

void FileOperator::sendRecursiveCreateFolderReq(FileTreeNode * selectedNode, QString newName)
{
    if (myState != FileOperatorState::REC_UPLOAD) return;

    qCDebug(fileManager, "Starting Recursive mkdir procedure: %s at %s",
           qPrintable(selectedNode->getFileData().getFullPath()),
           qPrintable(newName));
    RemoteDataReply * theReply = myInterface->mkRemoteDir(selectedNode->getFileData().getFullPath(), newName);

    QObject::connect(theReply, SIGNAL(haveMkdirReply(RequestState,FileMetaData)),
                     this, SLOT(getRecursiveMkdirReply(RequestState,FileMetaData)));
    myState = FileOperatorState::REC_UPLOAD_ACTIVE;
    return;
}

void FileOperator::sendRecursiveUploadReq(FileTreeNode * uploadTarget, QString localFile)
{
    if (myState != FileOperatorState::REC_UPLOAD) return;

    qCDebug(fileManager, "Starting recursively enacted upload procedure: %s to %s", qPrintable(localFile),
           qPrintable(uploadTarget->getFileData().getFullPath()));
    RemoteDataReply * theReply = myInterface->uploadFile(uploadTarget->getFileData().getFullPath(), localFile);

    QObject::connect(theReply, SIGNAL(haveUploadReply(RequestState,FileMetaData)),
                     this, SLOT(getRecursiveUploadReply(RequestState,FileMetaData)));
    myState = FileOperatorState::REC_UPLOAD_ACTIVE;
    return;
}

void FileOperator::emitStdFileOpErr(QString errString, RequestState errState)
{
    emit fileOpDone(errState, QString("%1: %2")
                    .arg(errString)
                    .arg(RemoteDataInterface::interpretRequestState(errState)));
}
