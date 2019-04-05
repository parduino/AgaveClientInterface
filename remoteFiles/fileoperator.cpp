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
#include "filerecursiveoperator.h"

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
    myRecursiveHandler = new FileRecursiveOperator(this);

    myModel.setColumnCount(tableNumCols);
    myModel.setHorizontalHeaderLabels(shownHeaderLabelList);

    QObject::connect(myInterface, SIGNAL(connectionStateChanged(RemoteDataInterfaceState)), this, SLOT(interfaceHasNewState(RemoteDataInterfaceState)), Qt::QueuedConnection);
}

FileOperator::~FileOperator()
{
    if (myRecursiveHandler != nullptr)
    {
        myRecursiveHandler->deleteLater();
    }

    delete rootFileNode;
}

void FileOperator::connectFileTreeWidget(RemoteFileTree * connectedWidget)
{
    if (connectedWidget == nullptr) return;
    connectedWidget->setModel(&myModel);
}

void FileOperator::disconnectFileTreeWidget(RemoteFileTree * connectedWidget)
{
    if (connectedWidget == nullptr) return;
    connectedWidget->setModel(nullptr);
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

QStandardItemModel *FileOperator::getStandardModel()
{
    return &myModel;
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

    if (myRecursiveHandler->getState() != RecursiveOpState::IDLE)
    {
        myRecursiveHandler->getRecursiveMkdirReply(replyState, newFolderData);
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

    if (myRecursiveHandler->getState() != RecursiveOpState::IDLE)
    {
        myRecursiveHandler->getRecursiveUploadReply(replyState, newFileData);
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

FileRecursiveOperator * FileOperator::getRecursiveOp()
{
    return myRecursiveHandler;
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

void FileOperator::fileNodesChange(FileNodeRef changedFile)
{
    emit fileSystemChange(changedFile);
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

const FileNodeRef FileOperator::speculateFileWithName(QString fullPath, bool folder, bool loadBuffer)
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
    return speculateFileWithName(scanNode->getFileData(), pathSoFar, folder, loadBuffer);
}

const FileNodeRef FileOperator::speculateFileWithName(const FileNodeRef &baseNode, QString addedPath, bool folder, bool loadBuffer)
{
    FileTreeNode * searchNode = getFileNodeFromNodeRef(baseNode);
    if (searchNode == nullptr) return FileNodeRef::nil();

    QStringList pathParts = FileMetaData::getPathNameList(addedPath);
    for (auto itr = pathParts.cbegin(); itr != pathParts.cend(); itr++)
    {
        FileTreeNode * nextNode = searchNode->getChildNodeWithName(*itr);
        if (nextNode != nullptr)
        {
            searchNode = nextNode;
            continue;
        }
        if (searchNode->getFileData().getFileType() != FileType::DIR)
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
    else if ((searchNode->getFileBuffer() == nullptr) && loadBuffer)
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

QPersistentModelIndex FileOperator::getModelIndexByFile(const FileNodeRef &theFile)
{
    FileTreeNode * theNode = getFileNodeFromNodeRef(theFile);
    if (theNode == nullptr) return QPersistentModelIndex();
    return theNode->getFirstModelIndex();
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
}

void FileOperator::emitStdFileOpErr(QString errString, RequestState errState)
{
    emit fileOpDone(errState, QString("%1: %2")
                    .arg(errString)
                    .arg(RemoteDataInterface::interpretRequestState(errState)));
}
