#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "fat32.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include "parser.h"
#include <stack>
#include <algorithm>
#include <ctime>

unsigned char lfn_checksum(const unsigned char *pFCBName)
{
   int i;
   unsigned char sum = 0;

   for (i = 11; i; i--)
      sum = ((sum & 1) << 7) + (sum >> 1) + *pFCBName++;

   return sum;
}

std::string int_to_month(int month)
{
    switch(month)
    {
    case 0:
        return "January";
    case 1:
        return "February";
    case 2:
        return "March";
    case 3:
        return "April";
    case 4:
        return "May";
    case 5:
        return "June";
    case 6:
        return "July";
    case 7:
        return "August";
    case 8:
        return "September";
    case 9:
        return "October";
    case 10:
        return "November";
    case 11:
        return "December";
    default:
        return "ERROR";
    }   
}

struct FileEntry {
    public:
        std::vector<FatFileLFN> lfns;
        FatFile83 file;
        std::string fileName;
        bool isDirectory;
        bool isDot;
        bool isDoubleDot;
        uint32_t firstClusterNum;

        FileEntry(std::vector<FatFileLFN> lfns, FatFile83 file){
            this->lfns = lfns;
            this->file = file;
            if(file.filename[0] == 0x2E){
                if(file.filename[1] == 0x2E){
                    this->isDoubleDot = true;
                    this->isDot = false;
                } else{
                    this->isDot = true;
                    this->isDoubleDot = false;
                }
            } else {
                this->isDot = false;
                this->isDoubleDot = false;
            }
            if( isDot){
                this->fileName = ".";
            }
            if( isDoubleDot){
                this->fileName = "..";
            }

            if( !isDot && !isDoubleDot ){
                if(lfns.size() > 1){
                    int brea = 0;
                }
                for(int i = 0; i < lfns.size(); i++){
                    bool last = false;
                    std::string fullName;
                    for(int j = 0; j<5; j++){
                        if(lfns[i].name1[j] == 0){
                            last = true;
                            fileName = fullName + fileName;
                            break;
                        }
                        fullName.push_back(char(lfns[i].name1[j]));
                    }
                    if(last)
                        continue;
                    for(int j = 0; j<6; j++){
                        if(lfns[i].name2[j] == 0){
                            last = true;
                            fileName = fullName + fileName;
                            break;
                        }
                        fullName.push_back(char(lfns[i].name2[j]));
                    }
                    if(last)
                        continue;
                    for(int j = 0; j<2; j++){
                        if(lfns[i].name3[j] == 0){
                            last = true;
                            fileName = fullName + fileName;
                            break;
                        }
                        fullName.push_back(char(lfns[i].name3[j]));
                    
                    }
                    fileName = fullName + fileName;
                } 
            }

            if(file.attributes == 16){
                this->isDirectory = true;
            } else {
                this->isDirectory = false;
            }

            firstClusterNum = file.eaIndex << 16 | file.firstCluster;

            if (firstClusterNum == 0) {
                firstClusterNum = firstClusterNum + 2;
            }
        }

        std::string getWithExtension(){
            if(this->isDirectory){
                return fileName;
            }
            std::string extension;
            extension.push_back('.');
            for(int j = 0; j<3; j++){
                char c = char(file.extension[j]);
                if(c == ' '){
                    return fileName;
                }
                extension.push_back(c);
            }
            return fileName + extension;
        }

        std::string getlsContent(){
            uint16_t date = file.modifiedDate;
            int year  = (date >> 9) + 1980;
            int month = (date >> 5) & 15;
            int day   = date & 31;

            uint16_t time = file.modifiedTime;
            int hour = (time >> 11);
            int minute = (time >> 5) & 63;
            std::string result;

            if(isDirectory){
                result.push_back('d');
            } else {
                result.push_back('-');
            }

            result.append(std::string("rwx------ 1 root root "));

            if(isDirectory){
                result.push_back('0');
            } else {
                result.append(std::to_string(file.fileSize));
            }

            result.push_back(' ');
            result.append(int_to_month(month));
            result.push_back(' ');
            result.append(std::to_string(day));
            result.push_back(' ');
            std::string paddedHour = std::to_string(hour);
            if (paddedHour.size() == 1) {
                paddedHour = std::string("0") + paddedHour;
            }
            result.append(paddedHour);
            result.push_back(':');
            std::string paddedMinute = std::to_string(minute);
            if (paddedMinute.size() == 1) {
                paddedMinute = std::string("0") + paddedMinute;
            }
            result.append(paddedMinute);
            result.push_back(' ');
            result.append(fileName);
            return result;
        }

        std::string getFileIndex(){
            std::string result;
             for(int j = 0; j<8; j++){
                result.push_back(char(file.filename[j]));
            }
            return result;
        }
};

struct Node {
    bool deleted = false;
    FileEntry* file;
    Node* parent;
    std::vector<Node*> childrens;

    Node(FileEntry* file, Node* parent){
        this->file = file;
        this->parent = parent;
    }
    Node(const Node& node)
    {
        this->childrens = node.childrens;
    }

    ~Node(){
        if(!deleted){
            for(int i = 0; i < childrens.size(); i++){
                delete childrens[i];
            }
        }
        delete file;
    }
};

BPB_struct bpb;
uint32_t root_cluster;
uint32_t eoc;
uint32_t dataStart;
uint32_t fatStart;
uint32_t secondFatStart;

FILE* fp;

Node* root = NULL;
Node* current_node = NULL;

uint32_t u8_to_u32(const uint8_t* bytes) {
  uint32_t u32 = (bytes[3] << 24) + (bytes[2] << 16) + (bytes[1] << 8) + bytes[0];
  return u32;
}

uint32_t getEOC(){
    fseek(fp,fatStart,SEEK_SET);
    
    uint8_t byte[4];
    for(int i = 0; i<4; i++){
        fread(&byte[i],sizeof(uint8_t),1,fp);
    }
    uint32_t nextClusterNum = u8_to_u32(byte);
    return nextClusterNum;
}

void allocateFat(uint32_t clusterNum, uint32_t value){
    
    fseek(fp,fatStart+(clusterNum*4),SEEK_SET);
    uint8_t* byte = new uint8_t[4];
    byte[3] = (value & 0xFF000000) >> 24;
    byte[2] = (value & 0x00FF0000) >> 16;
    byte[1] = (value & 0x0000FF00) >>  8;
    byte[0] = (value & 0x000000FF);

    for(int i = 0; i<4; i++){
        fwrite(&(byte[i]),sizeof(uint8_t),1,fp);
    }

    fseek(fp,secondFatStart+(clusterNum*4),SEEK_SET);
    for(int i = 0; i<4; i++){
        fwrite(&(byte[i]),sizeof(uint8_t),1,fp);
    }
    delete[] byte;
}

uint32_t getNextCluster(uint32_t clusterNum){
    fseek(fp,fatStart+(clusterNum*4),SEEK_SET);
    
    uint8_t byte[4];
    for(int i = 0; i<4; i++){
        fread(&byte[i],sizeof(uint8_t),1,fp);
    }
    uint32_t nextClusterNum = u8_to_u32(byte);
    return nextClusterNum;
}

uint32_t getFreeCluster(){
    uint32_t clusterNum = 2;
    uint8_t firstByte;
    while(true){
        fseek(fp,dataStart + bpb.BytesPerSector * bpb.SectorsPerCluster * (clusterNum - 2),SEEK_SET);
        fread(&firstByte,1,1,fp);
        if(firstByte == 0x00){
            uint32_t nextCluster = getNextCluster(clusterNum);
            if(nextCluster == 0){
                return clusterNum;
            }
        }
        clusterNum++;
    }
}

void fillTree(uint32_t clusterNum, Node* node){

    FatFileEntry entry;
    std::vector<FileEntry*> fileEntries;
    std::vector<FatFileLFN> fileLFNs;

    while(clusterNum != eoc){
        fseek(fp,dataStart + bpb.BytesPerSector * bpb.SectorsPerCluster * (clusterNum - 2),SEEK_SET);
    
        int spc = bpb.SectorsPerCluster;
        int dps = (int(bpb.BytesPerSector) / 32 ) * spc;

        for (int i = 0; i < dps; i++) {
            fread(&entry,sizeof(FatFileEntry),1,fp);
            if(entry.msdos.filename[0] == 0xE5){
                continue;
            }
            if(entry.msdos.filename[0] == 0x00){
                break;
            }
            if(entry.msdos.filename[0] == 0x2E){
                continue;
            }
            if(entry.msdos.attributes == 0x0f) {
               fileLFNs.push_back(entry.lfn);
            }
            if(entry.msdos.attributes == 16 || entry.msdos.attributes == 32) {
                fileEntries.push_back(new FileEntry(fileLFNs,entry.msdos));
                fileLFNs.clear();
            }
        } 
        clusterNum = getNextCluster(clusterNum);
    }

    for(int i = 0; i < fileEntries.size(); i++){
        Node* newNode = new Node(fileEntries[i],node);
        node->childrens.push_back(newNode);

        if (newNode->file->isDirectory){
            fillTree(newNode->file->firstClusterNum,newNode);
        }
    }

}

std::vector<std::string> parseString(std::string path){
    std::string delim = "/";
    std::vector<std::string> fileNames;
    auto start = 0U;
    auto end = path.find(delim);
    while (end != std::string::npos)
    {
        std::string token = path.substr(start, end - start);
        fileNames.push_back(token == "" ? std::string("/") : token); 
        start = end + delim.length();
        end = path.find(delim, start);
    }

    fileNames.push_back(path.substr(start, end)); 
    return fileNames;
}

Node* searchTree(Node* node, std::string name){
        if(name == "/") {
            return root;
        } else if (name == "."){
            return node;
        } else if (name == ".."){
            return node->parent;
        } else {
            for(int i = 0; i < node->childrens.size(); i++){
                if( node->childrens[i]->file->fileName == name && node->childrens[i]->file->isDirectory){
                    return node->childrens[i];
                }
            }
        }

        return NULL;
}

void generatePathFromTree(){
    std::stack<std::string> stack;
    Node* tempNode = current_node;
    if(tempNode->parent == NULL){
        std::cout << "/> ";
        return;
    }

    while (tempNode->parent != NULL)
    {
        stack.push(tempNode->file->fileName);
        tempNode = tempNode->parent;
    }

    while(!stack.empty()){
        std::cout << "/" << stack.top();
        stack.pop();
    }
    std::cout << "> ";
}

Node* findEntry(std::string path){
    std::vector<std::string> fileNames =  parseString(path);
    Node* node = current_node;
    if( fileNames.back() == "")
        fileNames.pop_back();
    for(int i = 0; i < fileNames.size(); i++){
        node = searchTree(node,fileNames[i]);
        if(node == NULL){
            generatePathFromTree();
            return NULL;
        }       
    }

    return node;
}

Node* findEntryWithoutLast(std::vector<std::string> fileNames){
    Node* node = current_node;

    for(int i = 0; i < fileNames.size(); i++){
        node = searchTree(node,fileNames[i]);
        if(node == NULL){
            generatePathFromTree();
            return NULL;
        }       
    }

    return node;
}

void cd(char* path){
    std::string strPath(path == NULL ? "/" : path);
    Node* node = findEntry(strPath);
    if(node == NULL){
        return;
    }
    current_node = node;
    generatePathFromTree();
    
}

void ls(bool more, char* path){
    if(more){
        Node* node = current_node;

        if(path){
            std::string strPath(path);
            node = findEntry(strPath);
            if(node == NULL)
                return; 
        }

        for(int i = 0; i < node->childrens.size(); i++){
            std::cout << node->childrens[i]->file->getlsContent() << std::endl;
        }
    } else {
        Node* node = current_node;

        if(path){
            std::string strPath(path);
            node = findEntry(strPath);
            if(node == NULL)
                return; 
        }

        for(int i = 0; i < node->childrens.size(); i++){
            std::cout << node->childrens[i]->file->getWithExtension();

            if( i != node->childrens.size() - 1){
                std::cout << " ";
            } 
        }
        if(node->childrens.size() != 0)
            std::cout << std::endl;  
    }
    generatePathFromTree();
}

void init_dot_entries(FatFile83* file, bool isDouble, const FatFile83* source, uint32_t parentCluster){

    std::string fileName;
    fileName.push_back('.');
    if(isDouble){
        fileName.push_back('.');
        bool isRoot = parentCluster == root_cluster;
        file->eaIndex = isRoot ? 0 : parentCluster >> 16;
        file->firstCluster = isRoot ? 0 : parentCluster & 0x0000FFFF;
    } else {
        file->eaIndex = source->eaIndex;
        file->firstCluster = source->firstCluster;
    }

    int i = 0;
    for(i; i < fileName.size(); i++){
        file->filename[i] = fileName[i];
    }
    for(i; i < 8; i++){
        file->filename[i] = ' ';
    }
    for(i = 0; i < 3; i++){
        file->extension[i] = ' ';
    }
    file->attributes = source->attributes;
    file->fileSize = source->fileSize;
    file->reserved = source->reserved;
    file->creationTime = source->creationTime;
    file->creationDate = source->creationDate;
    file->modifiedDate = source->modifiedDate;
    file->modifiedTime = source->modifiedTime;
    file->creationTimeMs = source->creationTimeMs;
    file->lastAccessTime = source->lastAccessTime;
    
}

void write_for_new_dir(uint32_t clusterNum,const FatFile83* file,const Node* parentNode){
    int spc = bpb.SectorsPerCluster;
    int dps = (int(bpb.BytesPerSector) / 32 ) * spc;
    uint32_t clusterByte = bpb.BytesPerSector * bpb.SectorsPerCluster;

    fseek(fp,dataStart + clusterByte * (clusterNum - 2),SEEK_SET);

    FatFile83 thisDir;
    init_dot_entries(&thisDir,false,file,0);

    FatFile83 parentDir;
    init_dot_entries(&parentDir,true,file,parentNode->file == NULL ? root_cluster : parentNode->file->firstClusterNum);

    fwrite(&thisDir,sizeof(FatFileEntry),1,fp);
    fwrite(&parentDir,sizeof(FatFileEntry),1,fp);
}

int getIndexNumber(Node* parentNode){
    uint32_t clusterNum = parentNode->file == NULL ? root_cluster : parentNode->file->firstClusterNum;
    int spc = bpb.SectorsPerCluster;
    int dps = (int(bpb.BytesPerSector) / 32 ) * spc;
    uint32_t clusterByte = bpb.BytesPerSector * bpb.SectorsPerCluster;
    FatFileEntry entry;
    int indexNum = 1;
    while(clusterNum != eoc){
        fseek(fp,dataStart + clusterByte * (clusterNum - 2),SEEK_SET);
        for (int i = 0; i < dps; i++) {
            fread(&entry,sizeof(FatFileEntry),1,fp);
            if(entry.msdos.filename[0] == 0x00){
                break;
            }
            if(entry.msdos.attributes == 16 || entry.msdos.attributes == 32) {
                indexNum ++;
            }
        } 
        clusterNum = getNextCluster(clusterNum);
    }
    return indexNum;
}

void init_Fat_83(Node* parentNode, FatFile83* file, bool isDirectory,FatFile83* moveFile){
    file->attributes = isDirectory ? 16 : 32;
    int num =  getIndexNumber(parentNode);  // parentNode->childrens.size() + 1;
    std::string fileName;
    fileName.push_back('~');
    fileName.append(std::to_string(num));
    int i = 0;
    for(i; i < fileName.size(); i++){
        file->filename[i] = fileName[i];
    }
    for(i; i < 8; i++){
        file->filename[i] = ' ';
    }
    for(i = 0; i < 3; i++){
        file->extension[i] = ' ';
    }

    if ( moveFile != NULL){
        file->eaIndex = moveFile->eaIndex;
        file->firstCluster = moveFile->firstCluster;
    } else if (isDirectory){
        uint32_t freeCluster = getFreeCluster();
        allocateFat(freeCluster,eoc);
        file->eaIndex = freeCluster >> 16;
        file->firstCluster = freeCluster & 0x0000FFFF;
    } else {
        file->eaIndex = 0;
        file->firstCluster = 0;
    }

    file->fileSize = moveFile != NULL ? moveFile->fileSize : 0;
    file->reserved = 0;

    time_t currTime;
    currTime = time(NULL);
    tm*  date = localtime(&currTime);
    uint16_t HMS = uint16_t(date->tm_hour) << 11 | uint16_t(date->tm_min) << 5  | date->tm_sec;
    uint16_t YMD = uint16_t(date->tm_year) << 9 | uint16_t(date->tm_mon) << 5  | date->tm_mday;

    file->creationTime =  moveFile != NULL ? moveFile->creationTime : YMD;
    file->creationDate =  moveFile != NULL ? moveFile->creationDate : HMS;
    file->modifiedDate = YMD;
    file->modifiedTime = HMS;
    // SONRA
    file->creationTimeMs =  moveFile != NULL ? moveFile->creationTimeMs : currTime;
    file->lastAccessTime =  moveFile != NULL ? moveFile->lastAccessTime : currTime;
    
}

std::vector<FatFileLFN> init_Fat_LFNs(const FatFile83* file, std::vector<std::string> fileParts){
    std::vector<FatFileLFN> lfns;
    unsigned char* name = new unsigned char[11];
    int i;

    for(i = 0; i < 8; i++){
        name[i] = char(file->filename[i]);
    }
    for(int j = 0 ; j < 3; j++){
        name[i] = char(file->extension[j]);
        i++;
    }

    for (int i = 0; i < fileParts.size(); i++) {
        FatFileLFN lfn;
        int nameSize = fileParts[i].length();

        if (i == 0){
            lfn.sequence_number = 0x40 + fileParts.size();
        } else {
            lfn.sequence_number = fileParts.size() - i ;
        }

        lfn.attributes = 0x0f;
        lfn.firstCluster = 0x00;
        lfn.firstCluster = 0x0000;
        lfn.reserved = 0x00;
        lfn.checksum =  uint8_t(lfn_checksum(name));
        int k =0;
        for(int j = 0; j < 13; j++){
            if(j >= nameSize){
                if(j < 5){
                    if(k == 0){
                        lfn.name1[j] = 0;
                        k++;
                    } else{
                        lfn.name1[j] = 0xff;
                    }
                }else if( j < 11){
                    if(k == 0){
                        lfn.name2[j-5] = 0;
                        k++;
                    } else{
                        lfn.name2[j-5] = 0xff;
                    }
                }else {
                    if(k == 0){
                        lfn.name3[j-11] = 0;
                        k++;
                    } else{
                        lfn.name3[j-11] = 0xff;
                    }
                }
            } else {
                if(j < 5){
                    lfn.name1[j] = fileParts[i][j];
                }else if( j < 11){
                    lfn.name2[j-5] = fileParts[i][j];
                }else {
                    lfn.name3[j-11] = fileParts[i][j];
                }
            }
        }

        lfns.push_back(lfn);
    }

    delete[] name;
    return lfns;
}

void write_to_cluster(const Node* parentNode, const FatFile83* fatFile83,const std::vector<FatFileLFN>* lfns){
    unsigned long size = (lfns->size() + 1) * 32;
    uint32_t clusterNum = parentNode->parent == NULL ? root_cluster : parentNode->file->firstClusterNum;
    uint32_t prevCluster;
    FatFileEntry entry;
    int spc = bpb.SectorsPerCluster;
    int dps = (int(bpb.BytesPerSector) / 32 ) * spc;
    uint32_t clusterByte = bpb.BytesPerSector * bpb.SectorsPerCluster;
    int numToWrite = lfns->size() + 1;
    int count = 1;
    
    while(clusterNum != eoc){
        fseek(fp,dataStart + clusterByte * (clusterNum - 2),SEEK_SET);

        for (int i = 0; i < dps; i++) {
            fread(&entry,sizeof(FatFileEntry),1,fp);
            if(entry.msdos.filename[0] == 0x00){
                fseek(fp,dataStart + clusterByte * (clusterNum - 2) +i*32,SEEK_SET);
                if(count < numToWrite){
                    FatFileLFN lfn = (*lfns)[count - 1];
                    fwrite(&lfn,sizeof(FatFileEntry),1,fp);
                } else {
                    fwrite(fatFile83,sizeof(FatFileEntry),1,fp);
                    return;
                }
               count++;     
            }
        }
        prevCluster = clusterNum;
        clusterNum = getNextCluster(clusterNum);
        if(clusterNum == eoc){
            uint32_t nextCluster = getFreeCluster();
            allocateFat(prevCluster,nextCluster);
            allocateFat(nextCluster,eoc);
            clusterNum = nextCluster;
        }
    }
}

void updateModify(uint32_t destClusterNum,uint32_t sourceClusterNum,uint16_t date, uint16_t time){
    int spc = bpb.SectorsPerCluster;
    int dps = (int(bpb.BytesPerSector) / 32 ) * spc;
    uint32_t clusterByte = bpb.BytesPerSector * bpb.SectorsPerCluster;
    FatFileEntry entry;

    while(destClusterNum != eoc){
        fseek(fp,dataStart + clusterByte * (destClusterNum - 2),SEEK_SET);

        for (int i = 0; i < dps; i++) {
            fread(&entry,sizeof(FatFileEntry),1,fp);
            if(entry.msdos.attributes == 16 || entry.msdos.attributes == 32) {
                uint16_t firstCluster = entry.msdos.firstCluster;
                uint16_t eaIndex = entry.msdos.eaIndex;

                uint32_t clusterNum = eaIndex << 16 | firstCluster;

                if(clusterNum == sourceClusterNum){
                    fseek(fp,dataStart + clusterByte * (destClusterNum - 2) + i * 32 + 22,SEEK_SET);
                    fwrite(&time,sizeof(uint16_t),1,fp);
                    fwrite(&date,sizeof(uint16_t),1,fp);
                    return;
                }
            }
        } 
        destClusterNum = getNextCluster(destClusterNum);
    }

}

void createFile(Node* parentNode,std::string name,bool isDirectory,Node* moveNode){
    for(int i = 0; i < parentNode->childrens.size(); i++){
        if( parentNode->childrens[i]->file->fileName == name){
            return;
        }
    }
    std::vector<std::string> fileParts;
    for (unsigned i = 0; i < name.length(); i += 13) {
        fileParts.push_back(name.substr(i, 13)); 
    }

    std::reverse(fileParts.begin(), fileParts.end());
    FatFile83 fatFile83;
    init_Fat_83(parentNode,&fatFile83,isDirectory, moveNode == NULL ? NULL : &(moveNode->file->file));
    
    std::vector<FatFileLFN> lfns = init_Fat_LFNs(&fatFile83,fileParts);
    write_to_cluster(parentNode,&fatFile83,&lfns);
    FileEntry* newFile = new FileEntry(lfns,fatFile83);
    Node* newNode = moveNode != NULL ? new Node(*moveNode) : new Node(newFile,parentNode);
    if(moveNode != NULL){
        newNode->file = newFile;
        newNode->parent = parentNode;
    }
    parentNode->childrens.push_back(newNode);
    
    if(parentNode->file != NULL && moveNode == NULL){
        parentNode->file->file.modifiedDate = fatFile83.modifiedDate;
        parentNode->file->file.modifiedTime = fatFile83.modifiedTime;
        uint32_t destClusterNum = parentNode->parent->file == NULL ? root_cluster : parentNode->parent->file->file.firstCluster;
        updateModify(destClusterNum,parentNode->file->firstClusterNum,fatFile83.modifiedDate,fatFile83.modifiedTime);
    }
    if (isDirectory){
        write_for_new_dir(newFile->firstClusterNum,&fatFile83,parentNode);
    }
    
}

void mightyCreation(char* path,bool isDirectory){
    if(path == NULL){
        generatePathFromTree();
        return;
    }
    Node* node;
    std::string strPath(path);
    std::vector<std::string> fileNames =  parseString(path);
    std::string orgFile = fileNames.back();
    fileNames.pop_back();
    
    if (fileNames.empty()){
        node = current_node;
    } else {
        node = findEntryWithoutLast(fileNames);
    }
    if(node == NULL){
        return;
    }
    createFile(node,orgFile,isDirectory, NULL);
    generatePathFromTree();
}

void showFile(Node* parentNode,std::string name){
    uint32_t clusterNum = eoc;
    for(int i = 0; i < parentNode->childrens.size(); i++){
        if( parentNode->childrens[i]->file->fileName == name){
            clusterNum = parentNode->childrens[i]->file->firstClusterNum;
        }
    }
    if(clusterNum == eoc){
        return;
    }

    int spc = bpb.SectorsPerCluster;
    int dps = (int(bpb.BytesPerSector) / 32 ) * spc;
    uint32_t clusterByte = bpb.BytesPerSector * bpb.SectorsPerCluster;
    char ch;

    while(clusterNum != eoc){
        fseek(fp,dataStart + clusterByte * (clusterNum - 2),SEEK_SET);
        for(int i = 0; i < clusterByte; i++){
            fread(&ch,sizeof(char),1,fp);
            if(ch == '\0')
                break;
            std::cout << char(ch);
        }
        clusterNum = getNextCluster(clusterNum);
    }
    std::cout << std::endl;
}

void cat(char* path) {
    if(path == NULL){
        generatePathFromTree();
        return;
    }
    Node* node;
    std::string strPath(path);
    std::vector<std::string> fileNames =  parseString(path);
    std::string orgFile = fileNames.back();
    fileNames.pop_back();
    
    if (fileNames.empty()){
        node = current_node;
    } else {
        node = findEntryWithoutLast(fileNames);
    }
    if(node == NULL){
        return;
    }
    showFile(node,orgFile);
    generatePathFromTree();
}

bool checkNode(Node* node){
    if( node == NULL){
        return true;
    } else {
        return false;
    }
}

void moveFile(Node* parentNode,std::string name, Node* destNode){
    Node* node = NULL;
    for(int i = 0; i < parentNode->childrens.size(); i++){
        if( parentNode->childrens[i]->file->fileName == name){
            node = parentNode->childrens[i];
        }
    }
    if ( node == NULL){
        return;
    }
    for(int i = 0; i < destNode->childrens.size(); i++){
        if( destNode->childrens[i]->file->fileName == name){
           return;
        }
    }
    uint32_t clusterNum = parentNode->file == NULL ? root_cluster : parentNode->file->firstClusterNum;
    int spc = bpb.SectorsPerCluster;
    int dps = (int(bpb.BytesPerSector) / 32 ) * spc;
    uint32_t clusterByte = bpb.BytesPerSector * bpb.SectorsPerCluster;
    FatFileEntry entry;
    std::vector<FatFileLFN> srcLfns = node->file->lfns;
    FatFile83 srcFile = node->file->file;
    int deletedLfn = 0;
    int lfnCount = srcLfns.size();
    uint8_t del = 0xE5;

    while(clusterNum != eoc){
        fseek(fp,dataStart + clusterByte * (clusterNum - 2),SEEK_SET);
        for (int i = 0; i < dps; i++) {
            fread(&entry,sizeof(FatFileEntry),1,fp);

            if(entry.msdos.attributes == 0x0f){
                if(entry.lfn.checksum == srcLfns[deletedLfn].checksum){
                    fseek(fp,dataStart + clusterByte * (clusterNum - 2) +i*32,SEEK_SET);
                    fwrite(&del,sizeof(uint8_t),1,fp);
                    deletedLfn++;
                    fseek(fp,31,SEEK_CUR);
                }
            } else if (entry.msdos.attributes == 16 || entry.msdos.attributes == 32) {
                if(deletedLfn == lfnCount){
                    fseek(fp,dataStart + clusterByte * (clusterNum - 2) +i*32,SEEK_SET);
                    fwrite(&del,sizeof(uint8_t),1,fp);
                    break;
                }
            }
        } 
        clusterNum = getNextCluster(clusterNum);
    }



    createFile(destNode,name,node->file->isDirectory,node);
    
    auto it = parentNode->childrens.begin();
    while(it != parentNode->childrens.end()) {
        if((*it)->file->fileName == node->file->fileName) {
            it = parentNode->childrens.erase(it);
            node->deleted = true;
            delete node;
        } else {
            it++;
        }
    }
}

void mv(char* path,char* destPath) {
    if(path == NULL){
        generatePathFromTree();
        return;
    }
    Node* node;
    Node* destNode = findEntry(std::string(destPath));
    if( destNode == NULL){
        return;
    }
    if(destNode->file != NULL && destNode->file->file.attributes == 32){
        return;
    }
    std::string strPath(path);
    std::vector<std::string> fileNames =  parseString(path);
    std::string orgFile = fileNames.back();
    fileNames.pop_back();
    
    if (fileNames.empty()){
        node = current_node;
    } else {
        node = findEntryWithoutLast(fileNames);
    }
    if(node == NULL){
        return;
    }
    moveFile(node,orgFile,destNode);
    generatePathFromTree();
}

void makeTree(){
    root = new Node(NULL,NULL);
    current_node = root;
    fillTree(root_cluster,root);
}

int main(int argc, char* argv[]) {
    fp = fopen(argv[1],"r+");    
    fseek(fp,0,SEEK_SET);
    fread(&bpb,sizeof(BPB_struct),1,fp);
    root_cluster = bpb.extended.RootCluster;
    fatStart = bpb.BytesPerSector * bpb.ReservedSectorCount;
    uint32_t fatSize = bpb.BytesPerSector * bpb.extended.FATSize;
    secondFatStart = fatStart + fatSize;
    dataStart = fatStart + fatSize * bpb.NumFATs;
    eoc = getEOC();
 
    makeTree();
    
    std::cout << "/> ";
    std::string line;

    while(std::getline(std::cin, line) && line.compare("quit")) {
        parsed_input *input = new parsed_input ;
        char* cline = new char[line.size() + 2];
        std::copy(line.begin(), line.end(), cline);
        cline[line.size()] = '\n';
        cline[line.size()+1] = '\0';
        parse(input,cline);
        
        if(input->type == CD) {
            // CONSIDER NULL
            cd(input->arg1);
        } else if(input->type == LS){
            char* more = input->arg1;
            char* path = input->arg2;
            if(more != NULL && strcmp(more,"-l") != 0){
                ls(false,more);
            }else {
                ls(more != NULL ? true : false,path);
            }
            
        } else if(input->type == MKDIR){
            char* path = input->arg1;
            mightyCreation(path,true);
        } else if(input->type == TOUCH){
            char* path = input->arg1;
            mightyCreation(path,false);
        } else if(input->type == CAT){
            cat(input->arg1);
        } else if(input->type == MV){
            char* source = input->arg1;
            char* dest = input->arg2;
            mv(source,dest);
        }
        clean_input(input);
    }   

    fclose(fp);
    delete root;
}

