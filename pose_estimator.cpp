#include "pose_estimator.h"
#include <torch/script.h>
#include <vector>
#include <string>
#include <opencv2/core/core.hpp>
#include <simple_transform.h>
using namespace std;
PoseEstimator::PoseEstimator(const char* model_path, int m_num_joints){
    _load_model(model_path);
    num_joints = m_num_joints;
}

void PoseEstimator::_load_model(const char* model_path){
    const torch::Device device = torch::Device(torch::kCUDA, 0);
    model = torch::jit::load(model_path, device);
}

void PoseEstimator::_preprocess(const cv::Mat& orig_img, 
                    const vector<bbox_t>& boxes,  // 
                    vector<at::Tensor>& inps,  // 
                    vector<bbox_t>& n_boxes){
    cv::Mat rgbMat;
    cv::cvtColor(orig_img, rgbMat, cv::ColorConversionCodes::COLOR_BGR2RGB);
    for(int i = 0; i < boxes.size(); i++){
        scale_t target_scale{INPUT_W, INPUT_H};
        bbox_t& box = n_boxes[i];
        test_transformation(rgbMat, boxes[i], inps[i], box, target_scale, 1);
    }
}

//TODO: sua mat -> tensor
void PoseEstimator::_post_process(const at::Tensor& heatmap,
                        const vector<bbox_t>& boxes,
                        vector<pose_t>& poses){
    int num_joints = heatmap.sizes()[1];
    for (int obj_idx = 0; obj_idx < boxes.size(); obj_idx++){
        at::Tensor kp_coords = torch::empty({num_joints, 2});
        at::Tensor kp_scores = torch::empty({num_joints});
        at::Tensor obj_hm = heatmap[obj_idx];
        heatmap_to_keypoints(obj_hm, boxes[obj_idx], kp_coords, kp_scores);
        for (int i = 0; i < num_joints; i++){
            int x = kp_coords[i][0].item().to<int>();
            int y = kp_coords[i][1].item().to<int>();
            float prob = kp_scores[i].item().to<float>();
            poses[obj_idx].keypoints[i].x = x;
            poses[obj_idx].keypoints[i].y = y;
            poses[obj_idx].keypoints[i].prob = prob;
        }
        // poses[obj_idx].bounding_box = boxes[obj_idx]; // copy lieu co duoc ko ?
    }
}

void PoseEstimator::predict(const cv::Mat& orig_img, 
                    const vector<bbox_t>& detected_boxes,
                    vector<pose_t>& preds){
    at::Tensor inp = torch::empty({1, 3, INPUT_H, INPUT_W});
    vector<at::Tensor> inps (detected_boxes.size(), inp);
    vector<bbox_t> n_boxes (detected_boxes.size()); // co phai neu item co kich thuoc co dinh thi ko can phai chi ra 2 tham so ?
    _preprocess(orig_img, detected_boxes, inps, n_boxes);
    at::Tensor hm = torch::empty({num_joints, HEATMAP_H, HEATMAP_W});
    vector<at::Tensor> hms; 
    for (int i = 0; i < inps.size(); i++){
        std::vector<torch::jit::IValue> input_batch;
        input_batch.push_back(inps[i]);
        at::Tensor tmp = model.forward(input_batch).toTensor();
        hms.push_back(tmp);
    }
    at::Tensor heatmap = torch::cat(hms);
    _post_process(heatmap, n_boxes, preds);
}
