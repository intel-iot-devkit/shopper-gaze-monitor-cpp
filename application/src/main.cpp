/*
* Copyright (c) 2018 Intel Corporation.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// Std includes
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <csignal>
#include <string>
#include <fstream>
// OpenCV includes
#include "inference.hpp"
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/video.hpp>
#include <nlohmann/json.hpp>

// MQTT
#include "mqtt.h"

using namespace std;
using namespace cv;
using json = nlohmann::json;
json jsonobj;

// Flag to control background threads
atomic<bool> keepRunning(true);

// OpenCV-related variables
bool poseChecked = false;
bool isAsyncmode = true;

// Application parameters
int rate;

// shoppingInfo contains statistics for the shopping information tracked by this application.
struct ShoppingInfo
{
    int shoppers;
    int lookers;
};

// currentInfo contains the latest ShoppingInfo tracked by the application.
ShoppingInfo currentInfo;

std::queue<Mat> nextImage;
String currentPerf;

std::mutex m, m1, m2;

const cv::String keys =
    "{ help  h     | | Print help message. }"
    "{ device d    | | Device to run the inference (CPU, GPU, MYRIAD, FPGA or HDDL only).}"
    "{ input i     | | Path to input image or video file.}"
    "{ model m     | | Path to .xml file of model containing face recognizer. }"
    "{ posemodel pm | | Path to .xml file of face pose model. }"
    "{ flag      f | | flag to run on sync or async mode. }"
    "{ rate r      | 1 | number of seconds between data updates to MQTT server. }";

// nextImageAvailable returns the next image from the queue in a thread-safe way
Mat nextImageAvailable()
{
    Mat rtn;
    m.lock();
    if (!nextImage.empty())
    {
        rtn = nextImage.front();
        nextImage.pop();
    }
    m.unlock();
    return rtn;
}

// addImage adds an image to the queue in a thread-safe way
void addImage(Mat img)
{
    m.lock();
    if (nextImage.empty())
    {
        nextImage.push(img);
    }
    m.unlock();
}

// getCurrentInfo returns the most-recent ShoppingInfo for the application.
ShoppingInfo getCurrentInfo()
{
    ShoppingInfo rtn;
    m2.lock();
    rtn = currentInfo;
    m2.unlock();
    return rtn;
}

/* updateInfo updates the current ShoppingInfo for the application to the highest values
   during the current time period.*/
void updateInfo(ShoppingInfo info)
{
    m2.lock();
    if (currentInfo.shoppers < info.shoppers)
    {
        currentInfo.shoppers = info.shoppers;
    }

    if (currentInfo.lookers < info.lookers)
    {
        currentInfo.lookers = info.lookers;
    }
    m2.unlock();
}

// resetInfo resets the current ShoppingInfo for the application.
void resetInfo()
{
    m2.lock();
    currentInfo.shoppers = 0;
    currentInfo.lookers = 0;
    m2.unlock();
}

// getCurrentPerf returns a string with the current performance stats for the Inference Engine.
string getCurrentPerf()
{
    string rtn;
    m1.lock();
    rtn = currentPerf;
    m1.unlock();
    return rtn;
}

// savePerformanceInfo sets the string with the current performance stats for the Inference Engine.
void savePerformanceInfo(double infer_time_face, double infer_time_pose)
{
    m1.lock();
    std::string label;
    double t = infer_time_face * 1000;
    double t2;
    if (poseChecked)
    {
        t2 = infer_time_pose * 1000;
    }
    if(isAsyncmode)
        label = format("Face inference time: N/A for Async mode, Pose inference time: N/A for Async mode");
    else
        label = format("Face inference time: %.2f ms, Pose inference time: %.2f ms", t * 1000, t2 * 1000);
    currentPerf = label;
    m1.unlock();
}

// Publish MQTT message with a JSON payload
void publishMQTTMessage(const string &topic, const ShoppingInfo &info)
{
    std::ostringstream list;
    list << "{\"shoppers\": \"" << info.shoppers << "\",";
    list << "\"lookers\": \"" << info.lookers << "\"}";
    std::string payload = list.str();

    mqtt_publish(topic, payload);

    string msg = "MQTT message published to topic: " + topic;
}

// Message handler for the MQTT subscription for the any desired control channel topic
int handleMQTTControlMessages(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    string topic = topicName;
    string msg = "MQTT message received: " + topic;
    return 1;
}

// Function called by worker thread to process the next available video frame.
void frameRunner(Network net, Network net_pose)
{
    while (keepRunning.load())
    {
        Mat next = nextImageAvailable();
        if (!next.empty())
        {
            std::chrono::duration<float> infer_time_face;
            std::chrono::duration<float> infer_time_pose;
            cv::Mat rsImg, rsImg_pose;
            cv::resize(next, rsImg, cv::Size(net.getModelWidth(), net.getModelHeight()));
            net.fillInputBlob(rsImg);
            std::chrono::high_resolution_clock::time_point infer_start_time = std::chrono::high_resolution_clock::now();
            net.inferenceRequest();
            std::chrono::high_resolution_clock::time_point infer_end_time = std::chrono::high_resolution_clock::now();
            infer_time_face = std::chrono::duration_cast<std::chrono::duration<float>>(infer_end_time - infer_start_time);
            net.wait();

            // Get inference results
            float *results = net.inference();

            // Get faces
            std::vector<float> confidences;
            std::vector<Rect> faces;
            int looking = 0;
            for (int i = 0; i < net.maxProposalCount; i++)
            {
                float *result = results + i * net.objectSize;
                float imageID = result[0];
                int label = static_cast<int>(result[1]);
                float confidence = result[2];
                if (confidence > 0.5)
                {
                    int left = (int)(result[3] * next.cols);
                    int top = (int)(result[4] * next.rows);
                    int right = (int)(result[5] * next.cols);
                    int bottom = (int)(result[6] * next.rows);
                    int width = right - left + 1;
                    int height = bottom - top + 1;

                    faces.push_back(Rect(left, top, width, height));
                    confidences.push_back(confidence);
                }
            }

            // Look for poses
            for (auto const &r : faces)
            {
                // Make sure the face rect is completely inside the main Mat
                if ((r & Rect(0, 0, next.cols, next.rows)) != r)
                {
                    continue;
                }

                cv::Mat face = next(r);

                // Convert to 4d vector, and process thru neural network
                cv::resize(face, rsImg_pose, cv::Size(net_pose.getModelWidth(), net_pose.getModelHeight()));

                net_pose.fillInputBlob(rsImg_pose);

                std::chrono::high_resolution_clock::time_point infer_start_time_pose = std::chrono::high_resolution_clock::now();

                net_pose.inferenceRequest();
                std::chrono::high_resolution_clock::time_point infer_end_time_pose = std::chrono::high_resolution_clock::now();
                infer_time_pose = std::chrono::duration_cast<std::chrono::duration<float>>(infer_end_time_pose - infer_start_time_pose);
                net_pose.wait();
                float *outs = net_pose.inference();
                poseChecked = true;

                // The shopper is looking if their head is tilted within a 45 degree angle relative to the shelf
                if ((outs[0] > -22.5) && (outs[0] < 22.5) &&
                    (outs[1] > -22.5) && (outs[1] < 22.5))
                {
                    looking++;
                }
            if(isAsyncmode)
                net_pose.swapInferenceRequest();

            }

            // Retail data
            ShoppingInfo info;
            info.shoppers = faces.size();
            info.lookers = looking;
            updateInfo(info);

            savePerformanceInfo(infer_time_face.count(), infer_time_pose.count());
            if(isAsyncmode)
                net.swapInferenceRequest();
        }

    }
    cout << "Video processing thread stopped" << endl;
}

// Function called by worker thread to handle MQTT updates. Pauses for rate second(s) between updates.
void messageRunner()
{
    while (keepRunning.load())
    {
        publishMQTTMessage("retail/traffic", getCurrentInfo());
        resetInfo();
        std::this_thread::sleep_for(std::chrono::seconds(rate));
    }
    cout << "MQTT sender thread stopped" << endl;
}

int main(int argc, char **argv)
{
    // Parse command parameters
    CommandLineParser parser(argc, argv, keys);

    std::string conf_modelLayers;
    std::string conf_modelWeights;
    std::string conf_modelLayers_pose;
    std::string conf_modelWeights_pose;
    std::string myTargetDevice;
    std::string conf_file = "../resources/config.json";
    Mat frame;
    VideoCapture cap;
    int delay = 5;
    String input, flag;
    Network net, net_pose;
    std::ifstream confFile(conf_file);
    confFile>>jsonobj;
    parser.about("Use this script to using OpenVINO.");
    if (argc == 1 || parser.has("help"))
    {
        parser.printMessage();

        return 0;
    }

    if (parser.has("device"))
    {
        myTargetDevice = parser.get<cv::String>("device");
    }
    else
    {
        myTargetDevice = "CPU";
    }

    if (parser.has("model"))
    {
        conf_modelLayers = parser.get<cv::String>("model");
        int pos = conf_modelLayers.rfind(".");
        conf_modelWeights = conf_modelLayers.substr(0, pos) + ".bin";
        /*
        if (myTargetDevice.find("CPU") != std::string::npos)
        {
            net.plugin.AddExtension(std::make_shared<InferenceEngine::Extensions::Cpu::CpuExtensions>(), "CPU");
        }
        */
        if (net.loadNetwork(conf_modelLayers, conf_modelWeights, net.ie, myTargetDevice) != 0)
            return EXIT_FAILURE;
    }
    else
    {
        std::cout << "Please specify xml model path for face detection.\n";
        return 0;
    }

    if (parser.has("posemodel"))
    {
        conf_modelLayers_pose = parser.get<cv::String>("posemodel");
        int pos = conf_modelLayers_pose.rfind(".");
        conf_modelWeights_pose = conf_modelLayers_pose.substr(0, pos) + ".bin";
        if (net_pose.loadNetwork(conf_modelLayers_pose, conf_modelWeights_pose, net.ie, myTargetDevice) != 0)
            return EXIT_FAILURE;
    }
    else
    {
        std::cout << "Please specify xml model path for face pose.\n";
        return 0;
    }
    if (parser.has("flag"))
    {
        flag = parser.get<String>("flag");
        if(flag == "async")
        {
            net.isAsync = 1;
            net_pose.isAsync = 1;
            std::cout<<"Application running in async mode"<<endl;
        }
        else
        {
            std::cout<<"Application running in sync mode"<<endl;
            isAsyncmode = false;
            net.isAsync = 0;
            net_pose.isAsync = 0;
        }
    }
    else
    {
         std::cout<<"Application running in async mode"<<endl;
         net.isAsync = 1;
         net_pose.isAsync = 1;
    }
    rate = parser.get<int>("rate");
    auto obj = jsonobj["inputs"];
    input = obj[0]["video"];

    // Connect MQTT messaging
    int result = mqtt_start(handleMQTTControlMessages);
    if (result == 0)
    {
        std::cout << "MQTT Started" << std::endl;
    }
    else
    {
        std::cout << "MQTT not started: have you set the ENV variables?" << std::endl;
    }

    mqtt_connect();

    if (input.size() == 1 && *(input.c_str()) >= '0' && *(input.c_str()) <= '9')
        cap.open(std::stoi(input));
    else
        cap.open(input);

    if (!cap.isOpened())
    {
        cerr << "ERROR! Unable to open video source\n";
        return -1;
    }

    // Also adjust delay so video playback matches the number of FPS in the file
    double fps = cap.get(CAP_PROP_FPS);
    delay = 1000 / fps;

    // Start worker threads
    std::thread t1(frameRunner, net, net_pose);
    std::thread t2(messageRunner);

    // Read video input data
    for (;;)
    {
        cap.read(frame);

        if (frame.empty())
        {
            cerr << "ERROR! blank frame grabbed\n";
            keepRunning = false;
            break;
        }

        addImage(frame);

        string label = getCurrentPerf();
        putText(frame, label, Point(0, 15), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 0, 0));

        ShoppingInfo info = getCurrentInfo();
        label = format("Shoppers: %d, lookers: %d", info.shoppers, info.lookers);
        putText(frame, label, Point(0, 40), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 0, 0));

        imshow("Shopper Gaze Monitor", frame);

        // TODO: signal threads to exit
        if (waitKey(delay) >= 0)
        {
            cout << "Attempting to stop background threads" << endl;
            keepRunning = false;
            break;
        }
    }

    // TODO: wait for worker threads to exit
    t1.join();
    t2.join();

    // Disconnect MQTT messaging
    mqtt_disconnect();
    mqtt_close();

    destroyAllWindows();
    cap.release();

    return 0;
}
