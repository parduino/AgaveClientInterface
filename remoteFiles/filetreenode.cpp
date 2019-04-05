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

#include "filetreenode.h"

#include "fileoperator.h"
#include "filenoderef.h"

#include "remoteJobs/jobstandarditem.h"

#include "filestandarditem.h"
#include "filemetadata.h"
#include "remotedatainterface.h"

FileTreeNode::FileTreeNode(FileMetaData contents, FileTreeNode * parent):QObject(qobject_cast<QObject *>(parent))
{
    myParent = parent;
    myFileOperator = myParent->myFileOperator;

    fileData.copyDataFrom(contents);
    fileData.setFileOperator(myFileOperator);
    settimestamps();

    myParent->childList.append(this);

    recomputeNodeState();
}

FileTreeNode::FileTreeNode(QString rootFolderName, FileOperator * theFileOperator, QObject * parent):QObject(qobject_cast<QObject *>(parent))
{
    QString fullPath = "/";
    fullPath = fullPath.append(rootFolderName);
    fullPath = RemoteDataInterface::removeDoubleSlashes(fullPath);

    myFileOperator = theFileOperator;

    fileData.setFullFilePath(fullPath);
    fileData.setType(FileType::DIR);
    settimestamps();
    fileData.setFileOperator(myFileOperator);
    nodeVisible = true;

    recomputeNodeState();
}

FileTreeNode::~FileTreeNode()
{
    //Note: DO NOT call delete directly on a file tree node except when
    //shutting down or resetting the file tree
    while (this->childList.size() > 0)
    {
        FileTreeNode * toDelete = this->childList.takeLast();
        delete toDelete;
    }
    purgeModelItems();

    if (this->fileDataBuffer != nullptr)
    {
        delete this->fileDataBuffer;
    }

    if (myParent != nullptr)
    {
        if (myParent->childList.contains(this))
        {
            myParent->childList.removeAll(this);
        }
    }
}

bool FileTreeNode::isRootNode()
{
    return (myParent == nullptr);
}

NodeState FileTreeNode::getNodeState()
{
    return myState;
}

FileNodeRef FileTreeNode::getFileData()
{
    return fileData;
}

QByteArray * FileTreeNode::getFileBuffer()
{
    return fileDataBuffer;
}

FileTreeNode * FileTreeNode::getNodeWithName(QString filename)
{
    return pathSearchHelper(filename,false);
}

FileTreeNode * FileTreeNode::getClosestNodeWithName(QString filename)
{
    return pathSearchHelper(filename,true);
}

FileTreeNode * FileTreeNode::getParentNode()
{
    return myParent;
}

FileTreeNode * FileTreeNode::getNodeReletiveToNodeWithName(QString searchPath)
{
    QStringList filePathParts = FileMetaData::getPathNameList(searchPath);
    return pathSearchHelperFromAnyNode(filePathParts, false);
}

void FileTreeNode::deleteFolderContentsData()
{
    folderContentsKnown = false;
    clearAllChildren();
}

void FileTreeNode::setFileBuffer(const QByteArray * newFileBuffer)
{
    if (fileDataBuffer != nullptr) delete fileDataBuffer;

    if (newFileBuffer == nullptr)
    {
        fileDataBuffer = nullptr;
    }
    else
    {
        fileDataBuffer = new QByteArray(*newFileBuffer);
    }

    setNodeVisible();
    recomputeNodeState();
}

bool FileTreeNode::haveLStask()
{
    return (lsTask != nullptr);
}

void FileTreeNode::setLStask(RemoteDataReply * newTask)
{
    if (newTask == nullptr) return;
    if (fileData.getFileType() != FileType::DIR)
    {
        qCDebug(fileManager, "ERROR: LS called on file rather than folder.");
        return;
    }
    if (lsTask != nullptr)
    {
        QObject::disconnect(lsTask, nullptr, this, nullptr);
    }
    lsTask = newTask;
    QObject::connect(lsTask, SIGNAL(haveLSReply(RequestState,QList<FileMetaData>)),
                     this, SLOT(deliverLSdata(RequestState,QList<FileMetaData>)));
    recomputeNodeState();
}

bool FileTreeNode::haveBuffTask()
{
    return (bufferTask != nullptr);
}

void FileTreeNode::setBuffTask(RemoteDataReply * newTask)
{
    if (newTask == nullptr) return;
    if (fileData.getFileType() != FileType::FILE)
    {
        qCDebug(fileManager, "ERROR: Buffer download called on non-file.");
        return;
    }
    if (bufferTask != nullptr)
    {
        QObject::disconnect(bufferTask, nullptr, this, nullptr);
    }
    bufferTask = newTask;
    QObject::connect(bufferTask, SIGNAL(haveBufferDownloadReply(RequestState,QByteArray)),
                     this, SLOT(deliverBuffData(RequestState,QByteArray)));
    recomputeNodeState();
}

QList<FileTreeNode *> FileTreeNode::getChildList()
{
    return childList;
}

FileTreeNode * FileTreeNode::getChildNodeWithName(QString filename)
{
    for (auto itr = this->childList.begin(); itr != this->childList.end(); itr++)
    {
        FileMetaData toSearch = (*itr)->getFileData();
        if ((toSearch.getFileName() == filename) && (toSearch.getFileType() != FileType::INVALID))
        {
            return (*itr);
        }
    }
    return nullptr;
}

bool FileTreeNode::isChildOf(FileTreeNode * possibleParent)
{
    //Note: In this method, a node is considered a child of itself
    FileTreeNode * nodeToCheck = this;

    if (possibleParent == nullptr) return false;

    while (nodeToCheck != nullptr)
    {
        if (nodeToCheck == possibleParent) return true;
        if (nodeToCheck->isRootNode()) return false;
        nodeToCheck = nodeToCheck->getParentNode();
    }
    return false;
}

QPersistentModelIndex FileTreeNode::getFirstModelIndex()
{
    if (modelItemList.isEmpty()) return QPersistentModelIndex();
    return modelItemList.first();
}

void FileTreeNode::deliverLSdata(RequestState taskState, QList<FileMetaData> dataList)
{
    lsTask = nullptr;
    if (taskState == RequestState::GOOD)
    {
        if (verifyControlNode(&dataList) == false)
        {
            qCDebug(fileManager, "ERROR: File tree data/node mismatch");
            recomputeNodeState();
            return;
        }
        this->updateFileNodeData(&dataList);
        return;
    }

    if (taskState == RequestState::FILE_NOT_FOUND)
    {
        changeNodeState(NodeState::DELETING);
        return;
    }
    else
    {
        qCDebug(fileManager, "Unable to connect to DesignSafe file server for ls task.");
        //TODO: switch folder contents to show connect error
    }

    recomputeNodeState();
}

void FileTreeNode::deliverBuffData(RequestState taskState, QByteArray bufferData)
{
    bufferTask = nullptr;
    if (taskState == RequestState::GOOD)
    {
        qCDebug(fileManager, "Download of buffer complete: %s", qPrintable(fileData.getFullPath()));
        if (bufferData.isNull())
        {
            setFileBuffer(nullptr);
        }
        else
        {
            setFileBuffer(&bufferData);
        }
        return;
    }

    if (taskState == RequestState::FILE_NOT_FOUND)
    {
        changeNodeState(NodeState::DELETING);
        return;
    }
    else
    {
        qCDebug(fileManager, "Unable to connect to DesignSafe file server for buffer task.");
        //TODO: switch folder contents to show connect error
    }
    recomputeNodeState();
}

void FileTreeNode::setNodeVisible()
{
    if (nodeVisible) return;
    nodeVisible = true;

    FileTreeNode * searchNode = getParentNode();
    if (searchNode != nullptr)
    {
        searchNode->setNodeVisible();
    }
    recomputeNodeState();
}

void FileTreeNode::recomputeNodeState()
{
    if (myState == NodeState::DELETING) return;
    if (fileData.getFileType() == FileType::DIR)
    {
        if (!nodeVisible)
        {
            if (haveLStask())
            {
                changeNodeState(NodeState::FOLDER_SPECULATE_LOADING); return;
            }
            else
            {
                changeNodeState(NodeState::FOLDER_SPECULATE_IDLE); return;
            }
        }

        if (haveLStask())
        {
            if (!childList.isEmpty())
            {
                changeNodeState(NodeState::FOLDER_CONTENTS_RELOADING); return;
            }

            changeNodeState(NodeState::FOLDER_CONTENTS_LOADING); return;
        }
        else
        {
            if (!folderContentsKnown)
            {
                changeNodeState(NodeState::FOLDER_KNOWN_CONTENTS_NOT); return;
            }
            changeNodeState(NodeState::FOLDER_CONTENTS_LOADED); return;
        }
    }
    else if (fileData.getFileType() == FileType::FILE)
    {
        if (!nodeVisible)
        {
            if (haveBuffTask())
            {
                changeNodeState(NodeState::FILE_SPECULATE_LOADING); return;
            }
            else
            {
                changeNodeState(NodeState::FILE_SPECULATE_IDLE); return;
            }
        }

        if (haveBuffTask())
        {
            if (fileDataBuffer != nullptr)
            {
                changeNodeState(NodeState::FILE_BUFF_RELOADING); return;
            }

            changeNodeState(NodeState::FILE_BUFF_LOADING); return;
        }
        else
        {
            if (fileDataBuffer == nullptr)
            {
                changeNodeState(NodeState::FILE_KNOWN); return;
            }

            changeNodeState(NodeState::FILE_BUFF_LOADED); return;
        }
    }
    changeNodeState(NodeState::ERROR);
}

void FileTreeNode::changeNodeState(NodeState newState)
{
    if (myState == NodeState::DELETING) return;
    if (newState == myState) return;
    myState = newState;

    if (myState == NodeState::DELETING)
    {
        this->deleteLater();
    }

    recomputeModelItems();
    if (!isRootNode()) myParent->recomputeModelItems();
    myFileOperator->fileNodesChange(fileData);
}

void FileTreeNode::recomputeModelItems()
{
    switch (myState) {
    case NodeState::DELETING:
    case NodeState::ERROR:
    case NodeState::NON_EXTANT:
        purgeModelItems();
        return;
    case NodeState::FILE_BUFF_LOADED:
    case NodeState::FILE_BUFF_LOADING:
    case NodeState::FILE_BUFF_RELOADING:
    case NodeState::FILE_KNOWN:
    case NodeState::FOLDER_CONTENTS_LOADING:
    case NodeState::FOLDER_CONTENTS_RELOADING:
    case NodeState::FOLDER_KNOWN_CONTENTS_NOT:
        updateModelItems(false);
        return;
    case NodeState::FOLDER_CONTENTS_LOADED:
        updateModelItems(true);
        return;
    case NodeState::FILE_SPECULATE_IDLE:
    case NodeState::FILE_SPECULATE_LOADING:
    case NodeState::FOLDER_SPECULATE_IDLE:
    case NodeState::FOLDER_SPECULATE_LOADING:
    case NodeState::INIT:
        return;
    }
}

void FileTreeNode::purgeModelItems()
{
    if (modelItemList.isEmpty()) return;

    if (decendantPlaceholderItem.isValid())
    {
        myFileOperator->myModel.removeRow(decendantPlaceholderItem.row(), modelItemList.first());
    }

    QModelIndex parentItem = modelItemList.first().parent();

    if (parentItem.isValid())
    {
        myFileOperator->myModel.removeRow(modelItemList.first().row(), parentItem);
    }
    else
    {
        myFileOperator->myModel.removeRow(modelItemList.first().row());
    }

    modelItemList.clear();
    decendantPlaceholderItem = QPersistentModelIndex();
}

void FileTreeNode::updateModelItems(bool folderContentsLoaded)
{
    if (modelItemList.isEmpty())
    {
        int i = 0;
        QList<QStandardItem *> tempRowList;
        while (i < myFileOperator->getStandardModel()->columnCount())
        {
            QStandardItem * itemToInsert = new FileStandardItem(fileData,myFileOperator->getStandardModel()->horizontalHeaderItem(i)->text());
            tempRowList.append(itemToInsert);
            i++;
        }
        if (myParent == nullptr)
        {
            myFileOperator->getStandardModel()->appendRow(tempRowList);
        }
        else
        {
            QPersistentModelIndex parentIndex = myParent->modelItemList.first();
            if (!parentIndex.isValid())
            {
                decendantPlaceholderItem = QPersistentModelIndex();
                modelItemList.clear();
                for (QStandardItem * anItem : tempRowList)
                {
                    delete anItem;
                }
                return;
            }
            myFileOperator->getStandardModel()->itemFromIndex(parentIndex)->appendRow(tempRowList);
        }

        for (QStandardItem * anItem : tempRowList)
        {
            QPersistentModelIndex anIndex(anItem->index());
            modelItemList.append(anIndex);
        }
    }
    else
    {
        for (QPersistentModelIndex anIndex : modelItemList)
        {
            dynamic_cast<FileStandardItem *>(myFileOperator->myModel.itemFromIndex(anIndex))->updateText(fileData);
        }
    }

    if (decendantPlaceholderItem.isValid())
    {
        myFileOperator->myModel.itemFromIndex(modelItemList.first())->removeRow(decendantPlaceholderItem.row());
        decendantPlaceholderItem = QPersistentModelIndex();
    }

    if (fileData.getFileType() != FileType::DIR) return;

    for (FileTreeNode * aChild : childList)
    {
        if (aChild->nodeVisible) return;
    }

    QStandardItem * newItem;
    if (folderContentsLoaded)
    {
        newItem = new FileStandardItem(FileNodeRef::nil(), "Empty");
    }
    else
    {
        newItem = new FileStandardItem(FileNodeRef::nil(), "Loading");
    }

    myFileOperator->getStandardModel()->itemFromIndex(modelItemList.first())->appendRow(newItem);
    decendantPlaceholderItem = QPersistentModelIndex(newItem->index());
}

void FileTreeNode::settimestamps()
{
    nodeTimestamp = QDateTime::currentMSecsSinceEpoch();
    fileData.setTimestamp(nodeTimestamp);
}

FileTreeNode * FileTreeNode::pathSearchHelper(QString filename, bool stopEarly)
{//I am worried about how generic this function is.
    //Our current agave setup has a named root folder
    if (isRootNode() == false) return nullptr;

    QStringList filePathParts = FileMetaData::getPathNameList(filename);
    if (filePathParts.isEmpty()) return nullptr;
    FileTreeNode * searchNode = this;

    QString rootName = filePathParts.takeFirst();
    if (rootName != searchNode->getFileData().getFileName())
    {
        return nullptr;
    }

    return searchNode->pathSearchHelperFromAnyNode(filePathParts, stopEarly);
}

FileTreeNode * FileTreeNode::pathSearchHelperFromAnyNode(QStringList filePathParts, bool stopEarly)
{
    FileTreeNode * searchNode = this;

    for (auto itr = filePathParts.cbegin(); itr != filePathParts.cend(); itr++)
    {
        FileTreeNode * nextNode = searchNode->getChildNodeWithName(*itr);
        if (nextNode == nullptr)
        {
            if (stopEarly == true)
            {
                return searchNode;
            }
            return nullptr;
        }
        searchNode = nextNode;
    }

    return searchNode;
}

bool FileTreeNode::verifyControlNode(QList<FileMetaData> * newDataList)
{
    QString controllerAddress = getControlAddress(newDataList);
    if (controllerAddress.isEmpty()) return false;

    FileTreeNode * myRootNode = this;
    while (myRootNode->myParent != nullptr)
    {
        myRootNode = myRootNode->myParent;
    }

    FileTreeNode * controllerNode = myRootNode->getNodeWithName(controllerAddress);
    return (controllerNode == this);
}

QString FileTreeNode::getControlAddress(QList<FileMetaData> * newDataList)
{
    for (auto itr = newDataList->cbegin(); itr != newDataList->cend(); itr++)
    {
        if ((*itr).getFileName() == ".")
        {
            return (*itr).getContainingPath();
        }
    }

    return "";
}

void FileTreeNode::updateFileNodeData(QList<FileMetaData> * newDataList)
{
    folderContentsKnown = true;

    //If the incoming list is empty, ie. has one entry (.), place empty file item
    if (newDataList->size() <= 1)
    {
        clearAllChildren();
        recomputeNodeState();
        return;
    }

    purgeUnmatchedChildren(newDataList);

    for (auto itr = newDataList->begin(); itr != newDataList->end(); itr++)
    {
        insertFile(&(*itr));
    }

    for (auto itr = childList.begin(); itr != childList.end(); itr++)
    {
        (*itr)->setNodeVisible();
    }

    recomputeNodeState();
}

void FileTreeNode::clearAllChildren()
{
    while (!childList.isEmpty())
    {
        FileTreeNode * aChild = childList.takeLast();
        aChild->changeNodeState(NodeState::DELETING);
    }
}

void FileTreeNode::insertFile(FileMetaData * newData)
{
    if (newData->getFileName() == ".") return;

    for (auto itr = childList.begin(); itr != childList.end(); itr++)
    {
        if ((newData->getFullPath() == (*itr)->getFileData().getFullPath()) &&
                (newData->getFileType() == (*itr)->getFileData().getFileType()))
        {
            if (newData->getSize() != (*itr)->getFileData().getSize())
            {
                (*itr)->getFileData().setSize(newData->getSize());
            }
            return;
        }
    }

    new FileTreeNode(*newData,this);
}

void FileTreeNode::purgeUnmatchedChildren(QList<FileMetaData> * newChildList)
{
    if (childList.size() == 0) return;

    QList<FileTreeNode *> altList;

    while (!childList.isEmpty())
    {
        FileTreeNode * aNode = childList.takeLast();
        FileMetaData toCheck = aNode->getFileData();

        bool matchFound = false;
        FileMetaData matchedData;

        for (auto itr = newChildList->begin(); itr != newChildList->end() && (matchFound == false); ++itr)
        {
            if ((*itr).getFileName() == ".") continue;

            if ((toCheck.getFullPath() == (*itr).getFullPath()) &&
                    (toCheck.getFileType() == (*itr).getFileType()))
            {
                matchFound = true;
                matchedData = *itr;
            }
        }

        if (matchFound)
        {
            altList.append(aNode);
        }
        else
        {
            aNode->changeNodeState(NodeState::DELETING);
        }
    }

    while (!altList.isEmpty())
    {
        childList.append(altList.takeLast());
    }
}
