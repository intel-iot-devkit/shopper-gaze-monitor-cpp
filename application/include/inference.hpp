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

#include <inference_engine.hpp>
//#include <ext_list.hpp>
#include <opencv2/imgproc.hpp>
#include <ie_icnn_net_reader.h>
//#include <ie_device.hpp>
//#include <ie_plugin_config.hpp>
//#include <ie_plugin_dispatcher.hpp>
//#include <ie_plugin_ptr.hpp>

class Network
{
  size_t modelWidth;
  size_t modelHeight;
  size_t modelChannels;
  size_t conf_batchSize;

public:
  int maxProposalCount;
  InferenceEngine::Core ie;
  InferenceEngine::InputsDataMap *inputInfo;
  int channelSize;
  int inputSize;
  int isAsync;
  std::string outputName;
  int objectSize;
  InferenceEngine::InferRequest::Ptr currInfReq;
  InferenceEngine::InferRequest::Ptr nextInfReq;
  InferenceEngine::SizeVector inputDims;
  InferenceEngine::SizeVector outputDims;
//  InferenceEngine::CNNNetReader networkReader;
  InferenceEngine::ExecutableNetwork network;
  const std::string *inputName = NULL;
  Network();
  int loadNetwork(std::string conf_modelLayers, std::string conf_modelWeights, InferenceEngine::Core ie, std::string myTargetDevice);
  template <typename T>
  void cvMatToBlob(const cv::Mat &img, InferenceEngine::Blob::Ptr &blob);
  size_t getModelHeight();
  size_t getModelWidth();
  void fillInputBlob(cv::Mat img);
  void inferenceRequest();
  void swapInferenceRequest();
  float *inference();
  void *wait();
};
