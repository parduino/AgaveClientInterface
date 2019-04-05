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

#include "filerecursiveoperator.h"

#include "fileoperator.h"
#include "filenoderef.h"
#include "remotedatainterface.h"
#include "filetreenode.h"

FileRecursiveOperator::FileRecursiveOperator(FileOperator *parent) : QObject(parent)
{
    myOperator = parent;
    myState = RecursiveOpState::IDLE;

    recursiveRemoteHead = FileNodeRef::nil();

    QObject::connect(myOperator, SIGNAL(fileSystemChange(FileNodeRef)),
                     this, SLOT(newFileSystemDataInterlock(FileNodeRef)), Qt::QueuedConnection);
    QObject::connect(this, SIGNAL(newFileInterlockSignal()),
                     this, SLOT(newFileSystemData()), Qt::QueuedConnection);
}

RecursiveOpState FileRecursiveOperator::getState()
{
    return myState;
}

void FileRecursiveOperator::enactRecursiveDownload(const FileNodeRef &targetFolder, QString containingDestFolder)
{
    if (myState != RecursiveOpState::IDLE) return;
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

    recursiveRemoteHead = targetFolder;
    myState = RecursiveOpState::REC_DOWNLOAD;
    emit fileOpStarted();
    recursiveDownloadProcessRetry();
}

void FileRecursiveOperator::enactRecursiveUpload(const FileNodeRef &containingDestFolder, QString localFolderToCopy)
{
    if (myState != RecursiveOpState::IDLE) return;
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

    recursiveRemoteHead = containingDestFolder;
    myState = RecursiveOpState::REC_UPLOAD;
    emit fileOpStarted();
    recursiveUploadProcessRetry();
}

void FileRecursiveOperator::abortRecursiveProcess()
{
    QString toDisplay = "Internal ERROR";

    if (myState != RecursiveOpState::REC_DOWNLOAD)
    {
        toDisplay = "Folder download stopped by user.";
    }
    else if (myState != RecursiveOpState::REC_UPLOAD)
    {
        toDisplay = "Folder upload stopped by user.";
    }
    else
    {
        return;
    }

    myState = RecursiveOpState::IDLE;
    fileOpDone(RequestState::STOPPED_BY_USER, toDisplay);
}

void FileRecursiveOperator::newFileSystemDataInterlock(FileNodeRef)
{
    if (interlockHasFileChange) return;
    interlockHasFileChange = true;
    emit newFileInterlockSignal();
}

void FileRecursiveOperator::newFileSystemData()
{
    interlockHasFileChange = false;
    if (myState == RecursiveOpState::REC_DOWNLOAD)
    {
        recursiveDownloadProcessRetry();
    }
    else if (myState == RecursiveOpState::REC_UPLOAD)
    {
        recursiveUploadProcessRetry();
    }
}

void FileRecursiveOperator::getRecursiveUploadReply(RequestState replyState, FileMetaData newFileData)
{
    if (myState != RecursiveOpState::REC_UPLOAD)
    {
        myState = RecursiveOpState::IDLE;
        return;
    }

    if (replyState != RequestState::GOOD)
    {
        myState = RecursiveOpState::IDLE;
        emitStdFileOpErr("Folder upload failed to upload file", replyState);
        return;
    }
    myOperator->lsClosestNodeToParent(newFileData.getFullPath());
}

void FileRecursiveOperator::getRecursiveMkdirReply(RequestState replyState, FileMetaData newFolderData)
{
    if (myState != RecursiveOpState::REC_UPLOAD)
    {
        myState = RecursiveOpState::IDLE;
        return;
    }

    if (replyState != RequestState::GOOD)
    {
        myState = RecursiveOpState::IDLE;
        emitStdFileOpErr("Folder upload failed to create new remote folder", replyState);
        return;
    }
    myOperator->lsClosestNode(newFolderData.getContainingPath());
}

void FileRecursiveOperator::recursiveDownloadProcessRetry()
{
    if (myState != RecursiveOpState::REC_DOWNLOAD)
    {
        myState = RecursiveOpState::IDLE;
        return;
    }

    if (recursiveDownloadRetrivalHelper(recursiveRemoteHead))
    {
        QString outText = "INTERNAL ERROR";
        RecursiveErrorCodes errNum = RecursiveErrorCodes::NONE;
        bool success = recursiveDownloadFolderEmitHelper(recursiveLocalHead, recursiveRemoteHead, errNum);
        if (success)
        {
            myState = RecursiveOpState::IDLE;
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

        myState = RecursiveOpState::IDLE;
        emit fileOpDone(RequestState::UNCLASSIFIED, outText);
        return;
    }
}

bool FileRecursiveOperator::recursiveDownloadRetrivalHelper(const FileNodeRef &nodeToCheck)
{
    if (nodeToCheck.getFileType() == FileType::FILE)
    {
        if (nodeToCheck.getFileBuffer() == nullptr)
        {
            myOperator->sendDownloadBuffReq(nodeToCheck);
            return false;
        }
        return true;
    }

    if (nodeToCheck.getFileType() != FileType::DIR) return true; //For now, we only copy files and folders

    bool foundAll = true;

    if (nodeToCheck.getNodeState() != NodeState::FOLDER_CONTENTS_LOADED)
    {
        foundAll = false;
        nodeToCheck.enactFolderRefresh();
    }

    for (FileNodeRef aChild : nodeToCheck.getChildList())
    {
        if (!recursiveDownloadRetrivalHelper(aChild))
        {
            foundAll = false;
        }
    }

    return foundAll;
}

bool FileRecursiveOperator::recursiveDownloadFolderEmitHelper(QDir currentLocalDir, const FileNodeRef &nodeToGet, RecursiveErrorCodes &errNum)
{
    if (nodeToGet.getFileType() != FileType::DIR)
    {
        errNum = RecursiveErrorCodes::TYPE_MISSMATCH;
        return false;
    }
    if (!currentLocalDir.exists())
    {
        errNum = RecursiveErrorCodes::LOST_FILE;
        return false;
    }
    for (FileNodeRef aChild : nodeToGet.getChildList())
    {
        if (aChild.getFileType() == FileType::DIR)
        {
            if (!currentLocalDir.mkdir(aChild.getFileName())) return false;
            QDir newFolder = currentLocalDir;
            newFolder.cd(aChild.getFileName());
            if (!newFolder.exists()) return false;
            if (!recursiveDownloadFolderEmitHelper(newFolder, aChild, errNum)) return false;
        }
        else if (aChild.getFileType() == FileType::FILE)
        {
            if (!emitBufferToFile(currentLocalDir, aChild, errNum)) return false;
        }
    }

    return true;
}

bool FileRecursiveOperator::emitBufferToFile(QDir containingDir, const FileNodeRef &nodeToGet, RecursiveErrorCodes &errNum)
{
    if (nodeToGet.getFileType() != FileType::FILE)
    {
        errNum = RecursiveErrorCodes::TYPE_MISSMATCH;
        return false;
    }

    if (!containingDir.exists())
    {
        errNum = RecursiveErrorCodes::LOST_FILE;
        return false;
    }

    if (containingDir.exists(nodeToGet.getFileName())) return false;

    QFile newFile(containingDir.absoluteFilePath(nodeToGet.getFileName()));
    if (nodeToGet.getFileBuffer() == nullptr) return false;
    if (!newFile.open(QFile::WriteOnly)) return false;
    if (newFile.write(nodeToGet.getFileBuffer()) < 0) return false;
    newFile.close();

    return true;
}

void FileRecursiveOperator::recursiveUploadProcessRetry()
{
    if (myState != RecursiveOpState::REC_UPLOAD) return;
    //TODO: If operator in use, we must wait

    RecursiveErrorCodes theError = RecursiveErrorCodes::NONE;
    FileNodeRef trueRemoteHead = recursiveRemoteHead.getChildWithName(recursiveLocalHead.dirName());
    if (trueRemoteHead.isNil())
    {
        myOperator->sendCreateFolderReq(recursiveRemoteHead, recursiveLocalHead.dirName());
        return;
    }

    if (recursiveUploadHelper(trueRemoteHead, recursiveLocalHead, theError))
    {
        myState = RecursiveOpState::IDLE;
        emit fileOpDone(RequestState::GOOD, "Folder uploaded.");
        return;
    }

    if (theError == RecursiveErrorCodes::NONE) return;

    myState = RecursiveOpState::IDLE;
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

bool FileRecursiveOperator::recursiveUploadHelper(const FileNodeRef &nodeToSend, QDir localPath, RecursiveErrorCodes &errNum)
{
    errNum = RecursiveErrorCodes::NONE;

    if (nodeToSend.getFileType() != FileType::DIR)
    {
        errNum = RecursiveErrorCodes::TYPE_MISSMATCH;
        return false;
    }

    if (nodeToSend.getNodeState() != NodeState::FOLDER_CONTENTS_LOADED)
    {
        nodeToSend.enactFolderRefresh();
        return false;
    }

    for (QFileInfo anEntry : localPath.entryInfoList(QDir::Dirs	| QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot))
    {
        if (anEntry.isDir())
        {
            QDir childDir = anEntry.dir();
            childDir.cd(anEntry.fileName());
            FileNodeRef childNode = nodeToSend.getChildWithName(childDir.dirName());
            if (childNode.isNil())
            {
                myOperator->sendCreateFolderReq(nodeToSend, childDir.dirName());
                return false;
            }
            if (!recursiveUploadHelper(childNode, childDir, errNum)) return false;
        }
        else if (anEntry.isFile())
        {
            FileNodeRef childNode = nodeToSend.getChildWithName(anEntry.fileName());
            if (childNode.isNil())
            {
                myOperator->sendUploadReq(nodeToSend, anEntry.absoluteFilePath());
                return false;
            }
            if (nodeToSend.getFileType() != FileType::FILE)
            {
                errNum = RecursiveErrorCodes::TYPE_MISSMATCH;
                return false;
            }
        }
    }

    return true;
}

void FileRecursiveOperator::emitStdFileOpErr(QString errString, RequestState errState)
{
    emit fileOpDone(errState, QString("%1: %2")
                    .arg(errString)
                    .arg(RemoteDataInterface::interpretRequestState(errState)));
}
