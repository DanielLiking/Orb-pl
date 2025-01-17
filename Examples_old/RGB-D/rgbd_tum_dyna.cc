/**
* This file is part of ORB-SLAM3
*
* Copyright (C) 2017-2021 Carlos Campos, Richard Elvira, Juan J. Gómez Rodríguez, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
* Copyright (C) 2014-2016 Raúl Mur-Artal, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
*
* ORB-SLAM3 is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM3 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
* the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with ORB-SLAM3.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <algorithm>
#include <fstream>
#include <chrono>


#include <unistd.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "System.h"
#include "Tracking.h"
#include "Logger.h"
#include "Utils.h"

using namespace std;

void LoadImages(const string &strAssociationFilename, vector<string> &vstrImageFilenamesRGB,
                vector<string> &vstrImageFilenamesD, vector<string> &vstrImageFilenamesMask,vector<double> &vTimestamps);

int main(int argc, char **argv)
{
    if(argc != 5)
    {
        cerr << endl << "Usage: ./rgbd_tum path_to_vocabulary path_to_settings path_to_sequence path_to_association" << endl;
        return 1;
    }

    // Retrieve paths to images
    vector<string> vstrImageFilenamesRGB;
    vector<string> vstrImageFilenamesD;
    vector<string> vstrImageFilenamesMask;
    vector<double> vTimestamps;
    string strAssociationFilename = string(argv[4]);
    LoadImages(strAssociationFilename, vstrImageFilenamesRGB, vstrImageFilenamesD, vstrImageFilenamesMask, vTimestamps);

    // Check consistency in the number of images and depthmaps
    int nImages = vstrImageFilenamesRGB.size();
    if(vstrImageFilenamesRGB.empty())
    {
        cerr << endl << "No images found in provided path." << endl;
        return 1;
    }
    else if(vstrImageFilenamesD.size()!=vstrImageFilenamesRGB.size())
    {
        cerr << endl << "Different number of images for rgb and depth." << endl;
        return 1;
    }

    cv::FileStorage fSettings(argv[2], cv::FileStorage::READ);
    bool bUseViewer = static_cast<int> (PLVS2::Utils::GetParam(fSettings, "Viewer.on", 1)) != 0;
    
    // Create SLAM system. It initializes all system threads and gets ready to process frames.
    PLVS2::System SLAM(argv[1],argv[2],PLVS2::System::RGBD,bUseViewer);
    float imageScale = SLAM.GetImageScale();

    // Vector for tracking time statistics
    vector<float> vTimesTrack;
    vTimesTrack.resize(nImages);

    cout << endl << "-------" << endl;
    cout << "Start processing sequence ..." << endl;
    cout << "Images in the sequence: " << nImages << endl << endl;
    //腐蚀参数
     int dilation_size = 15;
     cv::Mat kernel = getStructuringElement(cv::MORPH_ELLIPSE,
                                           cv::Size( 2*dilation_size + 1, 2*dilation_size+1 ),
                                           cv::Point( dilation_size, dilation_size ) );



    int numImgsNoInit = 0; 
    int numImgsLost = 0; 
    
    //nImages = 100; // force exit after few images (just for testing/debugging)
    
    // Main loop
    cv::Mat imRGB, imD;
    cv::Mat imMask;
    for(int ni=0; ni<nImages; ni++)
    {
        // Read image and depthmap from file
        imRGB = cv::imread(string(argv[3])+"/"+vstrImageFilenamesRGB[ni],cv::IMREAD_UNCHANGED); //,cv::IMREAD_UNCHANGED);
        imD = cv::imread(string(argv[3])+"/"+vstrImageFilenamesD[ni],cv::IMREAD_UNCHANGED); //,cv::IMREAD_UNCHANGED);
        imMask = cv::imread(string(argv[3])+"/"+vstrImageFilenamesMask[ni],cv::IMREAD_UNCHANGED);

        if (imD.size() != imMask.size()) {
            std::cerr << "Depth map and mask size mismatch: " << vstrImageFilenamesD[ni] << std::endl;
            continue;
        }


        double tframe = vTimestamps[ni];

        if(imRGB.empty())
        {
            cerr << endl << "Failed to load image at: "
                 << string(argv[3]) << "/" << vstrImageFilenamesRGB[ni] << endl;
            return 1;
        }
        if(imMask.empty())
        {
            
             cerr << endl << "Failed to load mask at: "
                 << string(argv[3]) << "/" << vstrImageFilenamesMask[ni] << endl;
            return 1;
        }
        if(imageScale != 1.f)
        {
            int width = imRGB.cols * imageScale;
            int height = imRGB.rows * imageScale;
            cv::resize(imRGB, imRGB, cv::Size(width, height));
            cv::resize(imD, imD, cv::Size(width, height));
        }

#ifdef COMPILEDWITHC11
        std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
#else
        std::chrono::monotonic_clock::time_point t1 = std::chrono::monotonic_clock::now();
#endif
        cv::Mat mask = cv::Mat::ones(480,640,CV_8U);
        cv::Mat mask_file = imMask.clone();
        cv::Mat mask_d = mask_file.clone();

        cv::dilate(mask_file,mask_d, kernel);
        cv::threshold(mask_d, mask_d, 1, 1, cv::THRESH_BINARY);
        mask = mask_d;

        cv::Mat maskZero = cv::Mat::zeros(mask.size(), CV_8U);
        cv::Mat maskIndices;

        // 找到掩码中值为0的像素位置
        cv::threshold(mask, maskZero, 0, 255, cv::THRESH_BINARY_INV);

        // 使用这些位置信息来更新深度图
        imD.setTo(cv::Scalar(1), maskZero);

 

        // Pass the image to the SLAM system
        // SLAM.TrackRGBD(imRGB,imD, mask, tframe);
           SLAM.TrackRGBDM(imRGB,imD, mask, tframe);
        int trackingState = SLAM.GetTrackingState();
        if(trackingState == PLVS2::Tracking::NOT_INITIALIZED) numImgsNoInit++;
        if(trackingState == PLVS2::Tracking::LOST) numImgsLost++;

#ifdef COMPILEDWITHC11
        std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
#else
        std::chrono::monotonic_clock::time_point t2 = std::chrono::monotonic_clock::now();
#endif

        double ttrack= std::chrono::duration_cast<std::chrono::duration<double> >(t2 - t1).count();

        vTimesTrack[ni]=ttrack;

        // Wait to load the next frame
        double T=0;
        if(ni<nImages-1)
            T = vTimestamps[ni+1]-tframe;
        else if(ni>0)
            T = tframe-vTimestamps[ni-1];
#if 1
        if(ttrack<T)
            usleep((T-ttrack)*1e6);
#else        
        std::cout<<"img " << ni << std::endl; 
        if(SLAM.GetTrackingState() != ORBSLAM3::Tracking::NOT_INITIALIZED)
        {
            getchar(); // step by step
        }
#endif
        
    }
    
    if(bUseViewer)
    {
        std::cout << "\n******************\n" << std::endl;
        std::cout << "press a key to end" << std::endl;
        std::cout << "\n******************\n" << std::endl;
        getchar();
    }

    // Stop all threads
    SLAM.Shutdown();

    // Tracking time statistics
    sort(vTimesTrack.begin(),vTimesTrack.end());
    float totaltime = 0;
    for(int ni=0; ni<nImages; ni++)
    {
        totaltime+=vTimesTrack[ni];
    }
    cout << "-------" << endl << endl;
    cout << "median tracking time: " << vTimesTrack[nImages/2] << endl;
    cout << "mean tracking time: " << totaltime/nImages << endl;
    
    cout << "perc images lost: " << (float(numImgsLost)/nImages)*100. << std::endl; 
    cout << "perc images no init: " << (float(numImgsNoInit)/nImages)*100. << std::endl;     

    cout << "-------" << endl << endl;
    
    // TODO readd print statistics 
    SLAM.PrintMapStatistics();
        
    // Save camera trajectory
    SLAM.SaveTrajectoryTUM("CameraTrajectory.txt");
    SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");   
    
    Logger logger("Performances.txt");
    logger << "perc images lost: " << (float(numImgsLost)/nImages)*100. << std::endl; 
    logger << "perc images no init: " << (float(numImgsNoInit)/nImages)*100. << std::endl; 
    logger << "median tracking time: " << vTimesTrack[nImages/2] << endl;
    logger << "mean tracking time: " << totaltime/nImages << endl;

    cout << "done!" << std::endl;
    
    return 0;
}

void LoadImages(const string &strAssociationFilename, vector<string> &vstrImageFilenamesRGB,
                vector<string> &vstrImageFilenamesD, vector<string> &vstrImageFilenamesMask, vector<double> &vTimestamps)
{
    ifstream fAssociation;
    fAssociation.open(strAssociationFilename.c_str());
    if (!fAssociation.is_open()) 
    { 
        std::cout << "cannot open file: " << strAssociationFilename << std::endl;
        quick_exit(-1);
    }

    while(!fAssociation.eof())
    {
        string s;
        getline(fAssociation,s);
        if(!s.empty())
        {
            stringstream ss;
            ss << s;
            double t;
            string sRGB, sD, sMask;
            ss >> t;
            vTimestamps.push_back(t);
            ss >> sRGB;
            vstrImageFilenamesRGB.push_back(sRGB);
            ss >> t;
            ss >> sD;
            vstrImageFilenamesD.push_back(sD);
               size_t pos = sRGB.find("rgb");
            if (pos != std::string::npos) {
                sMask = sRGB.substr(0, pos) + "mask" + sRGB.substr(pos + 3);
                vstrImageFilenamesMask.push_back(sMask);
            
            }

        }
    }
}
