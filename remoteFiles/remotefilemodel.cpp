/*********************************************************************************
**
** Copyright (c) 2018 The University of Notre Dame
** Copyright (c) 2018 The Regents of the University of California
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

#include "remotefilemodel.h"
#include "remotefileitem.h"
#include "remotefiletree.h"

#include "filenoderef.h"
#include "fileoperator.h"
#include "filetreenode.h"

RemoteFileModel::RemoteFileModel(QObject *parent) : QObject(parent)
{
    theModel.setColumnCount(tableNumCols);
    theModel.setHorizontalHeaderLabels(shownHeaderLabelList);
}

RemoteFileItem * RemoteFileModel::getItemByFile(FileNodeRef toFind)
{
    RemoteFileItem * parentNode = findParentItem(toFind);
    if (parentNode == nullptr) return nullptr;
    RemoteFileItem * nodeToFind = findTargetItem(parentNode, toFind);
    return nodeToFind;
}

QStandardItemModel * RemoteFileModel::getRawModel()
{
    return &theModel;
}

void RemoteFileModel::newFileData(FileNodeRef newFileData)
{
    if (newFileData.isRootNode())
    {
        if (userRoot == nullptr)
        {
            setRootItem(newFileData);
        }
        else
        {
            updateItemList(userRoot->getRowList(),newFileData);
        }
        return;
    }
    if (userRoot == nullptr) return;

    NodeState theState = newFileData.getNodeState();

    switch (theState) {
    case NodeState::DELETING:
    case NodeState::ERROR:
    case NodeState::NON_EXTANT:
        purgeItem(newFileData);
        return;
    case NodeState::FILE_BUFF_LOADED:
    case NodeState::FILE_BUFF_LOADING:
    case NodeState::FILE_BUFF_RELOADING:
    case NodeState::FILE_KNOWN:
    case NodeState::FOLDER_CONTENTS_LOADING:
    case NodeState::FOLDER_CONTENTS_RELOADING:
    case NodeState::FOLDER_KNOWN_CONTENTS_NOT:
        updateItem(newFileData);
        return;
    case NodeState::FOLDER_CONTENTS_LOADED:
        updateItem(newFileData,true);
        return;
    case NodeState::FILE_SPECULATE_IDLE:
    case NodeState::FILE_SPECULATE_LOADING:
    case NodeState::FOLDER_SPECULATE_IDLE:
    case NodeState::FOLDER_SPECULATE_LOADING:
    case NodeState::INIT:
        return;
    }
}

void RemoteFileModel::setRootItem(FileNodeRef rootFile)
{
    if (userRoot != nullptr) return;
    QList<RemoteFileItem *> userRootList = createItemList(rootFile);
    userRoot = userRootList.first();
    updateItemList(userRootList, rootFile);

    theModel.invisibleRootItem()->appendRow(demoteList(userRootList));
}

void RemoteFileModel::purgeItem(FileNodeRef toRemove)
{
    RemoteFileItem * parentItem = findParentItem(toRemove);
    if (parentItem == nullptr) return;
    RemoteFileItem * targetItem = findTargetItem(parentItem, toRemove);
    if (targetItem == nullptr) return;

    parentItem->removeRow(targetItem->row());
    if (parentItem->hasChildren()) return;
    if (parentItem->getFile().getNodeState() == NodeState::FOLDER_CONTENTS_LOADED)
    {
        parentItem->appendRow(new RemoteFileItem(false));
    }
    else
    {
        parentItem->appendRow(new RemoteFileItem(true));
    }
}

void RemoteFileModel::updateItem(FileNodeRef toUpdate, bool folderContentsLoaded)
{
    RemoteFileItem * parentItem = findParentItem(toUpdate);
    if (parentItem == nullptr) return;
    RemoteFileItem * targetItem = findTargetItem(parentItem, toUpdate);

    QList<RemoteFileItem *> itemList;
    if (targetItem == nullptr)
    {
        if (parentItem->parentOfPlaceholder())
        {
            while (parentItem->hasChildren())
            {
                parentItem->removeRow(0);
            }
        }

        itemList = createItemList(toUpdate);
        targetItem = itemList.at(0);
        parentItem->appendRow(demoteList(itemList));
    }
    else
    {
        itemList = targetItem->getRowList();
    }

    updateItemList(itemList, toUpdate);
    if (targetItem->parentOfPlaceholder())
    {
        while (targetItem->hasChildren()) targetItem->removeRow(0);
        targetItem->appendRow(new RemoteFileItem(!folderContentsLoaded));
        return;
    }

    if (targetItem->hasChildren() || (toUpdate.getFileType() != FileType::DIR)) return;
    targetItem->appendRow(new RemoteFileItem(!folderContentsLoaded));
}

QList<RemoteFileItem *> RemoteFileModel::createItemList(FileNodeRef theFileNode)
{
    QList<RemoteFileItem *> ret;
    RemoteFileItem * firstItem = new RemoteFileItem(theFileNode);
    ret.append(firstItem);
    while (ret.size() < theModel.columnCount())
    {
        ret.append(new RemoteFileItem(firstItem));
    }
    return ret;
}

RemoteFileItem * RemoteFileModel::findTargetItem(RemoteFileItem * parentItem, FileNodeRef toFind)
{
    for (int i = 0; i < parentItem->rowCount(); i++)
    {
        RemoteFileItem * compareItem = dynamic_cast<RemoteFileItem *>(parentItem->child(i,0));
        if (compareItem == nullptr) continue;
        FileNodeRef toCompare = compareItem->getFile();
        if (toCompare.getFileType() != toFind.getFileType()) continue;
        if (toCompare.getFileName() != toFind.getFileName()) continue;
        return compareItem;
    }
    return nullptr;
}

RemoteFileItem * RemoteFileModel::findParentItem(FileNodeRef toFind)
{
    if (userRoot == nullptr) return nullptr;

    QStringList fileParts = separateFilePathParts(toFind.getContainingPath());
    if (fileParts.size() <= 1) return userRoot;
    QString firstPath = fileParts.takeFirst();
    if (userRoot->getFile().getFileName() != firstPath)
    {
        qCDebug(fileManager, "ERROR: files given to remote file model from a different user folder.");
        return nullptr;
    }

    RemoteFileItem * searchItem = userRoot;
    for (QString pathPart : fileParts)
    {
        bool itemFound = false;
        for (int i = 0; (i < searchItem->rowCount()) && !itemFound; i++)
        {
            RemoteFileItem * compareItem = dynamic_cast<RemoteFileItem *>(searchItem->child(i,0));
            if (compareItem == nullptr) continue;
            FileNodeRef toCompare = compareItem->getFile();
            if (toCompare.getFileType() != FileType::DIR) continue;
            if (toCompare.getFileName() != pathPart) continue;
            searchItem = compareItem;
            itemFound = true;
        }
        if (!itemFound) return nullptr;
    }
    return searchItem;
}

QString RemoteFileModel::getRawColumnData(FileNodeRef fileData, int i)
{
    if (fileData.isNil())
    {
        return "";
    }

    QStandardItem * headerItem = theModel.horizontalHeaderItem(i);
    if (headerItem == nullptr)
    {
        return "";
    }
    QString headerText = headerItem->text();
    if (headerText == "File Name")
    {
        return fileData.getFileName();
    }
    if (headerText == "Type")
    {
        return fileData.getFileTypeString();
    }
    if (headerText == "Size")
    {
        return QString::number(fileData.getSize());
    }
    return "";
}

QList<QStandardItem *> RemoteFileModel::demoteList(QList<RemoteFileItem *> inputList)
{
    QList<QStandardItem *> ret;
    for (RemoteFileItem * anItem : inputList)
    {
        ret.append(anItem);
    }
    return ret;
}

QStringList RemoteFileModel::separateFilePathParts(QString thePath)
{
    QStringList allParts = thePath.split('/');
    QStringList ret;
    for (QString dirName : allParts)
    {
        if (dirName.isEmpty()) continue;
        ret.append(dirName);
    }
    return ret;
}

void RemoteFileModel::updateItemList(QList<RemoteFileItem *> theList, FileNodeRef newFileInfo)
{
    for (int i = 0; i < theList.size(); i++)
    {
        RemoteFileItem * itemToUpdate = theList.at(i);
        itemToUpdate->setText(getRawColumnData(newFileInfo, i));
    }
}
