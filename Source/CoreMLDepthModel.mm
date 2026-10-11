// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "CoreMLDepthModel.h"

#import <CoreML/CoreML.h>

#include <cstring>
#include <limits>

namespace {
std::string message(NSError* error) {
	return error == nil ? "Unknown Core ML error." : error.localizedDescription.UTF8String;
}
}

std::unique_ptr<CoreMLDepthModel> CoreMLDepthModel::load(
	const std::filesystem::path& path, int inputSize, std::string& error) {
	@autoreleasepool {
		error.clear();
		if (inputSize <= 0 || !std::filesystem::is_directory(path)) {
			error = "Compiled Core ML depth model was not found: " + path.string();
			return nullptr;
		}
		NSString* name = [NSString stringWithUTF8String:path.string().c_str()];
		NSURL* url = [NSURL fileURLWithPath:name isDirectory:YES];
		MLModelConfiguration* configuration = [[MLModelConfiguration alloc] init];
		configuration.computeUnits = MLComputeUnitsCPUAndGPU;
		NSError* nativeError = nil;
		MLModel* model = [MLModel modelWithContentsOfURL:url
			configuration:configuration error:&nativeError];
		if (model == nil) {
			error = message(nativeError);
			return nullptr;
		}
		MLFeatureDescription* input = model.modelDescription.inputDescriptionsByName[@"input"];
		NSArray<NSNumber*>* shape = input.multiArrayConstraint.shape;
		if (input.type != MLFeatureTypeMultiArray || shape.count != 4 ||
			shape[0].integerValue != 1 || shape[1].integerValue != 3 ||
			shape[2].integerValue != inputSize || shape[3].integerValue != inputSize ||
			model.modelDescription.outputDescriptionsByName.count != 1) {
			error = "Core ML depth model has unexpected inputs or outputs.";
			return nullptr;
		}
		auto result = std::unique_ptr<CoreMLDepthModel>(new CoreMLDepthModel());
		result->model_ = CFBridgingRetain(model);
		result->outputName_ = model.modelDescription.outputDescriptionsByName.allKeys.firstObject.UTF8String;
		result->inputSize_ = inputSize;
		return result;
	}
}

CoreMLDepthModel::~CoreMLDepthModel() {
	if (model_ != nullptr) CFBridgingRelease(model_);
}

bool CoreMLDepthModel::predict(const float* input, size_t count,
	std::vector<float>& output, int& width, int& height, std::string& error) const {
	@autoreleasepool {
		error.clear();
		const size_t expected = static_cast<size_t>(inputSize_) * inputSize_ * 3;
		if (input == nullptr || count != expected) {
			error = "Core ML depth input has the wrong size.";
			return false;
		}
		NSError* nativeError = nil;
		MLMultiArray* array = [[MLMultiArray alloc] initWithShape:@[@1, @3,
			@(inputSize_), @(inputSize_)] dataType:MLMultiArrayDataTypeFloat32
			error:&nativeError];
		if (array == nil) {
			error = message(nativeError);
			return false;
		}
		std::memcpy(array.dataPointer, input, count * sizeof(float));
		MLFeatureValue* value = [MLFeatureValue featureValueWithMultiArray:array];
		MLDictionaryFeatureProvider* features = [[MLDictionaryFeatureProvider alloc]
			initWithDictionary:@{@"input": value} error:&nativeError];
		if (features == nil) {
			error = message(nativeError);
			return false;
		}
		MLModel* model = (__bridge MLModel*)model_;
		id<MLFeatureProvider> prediction = [model predictionFromFeatures:features
			error:&nativeError];
		if (prediction == nil) {
			error = message(nativeError);
			return false;
		}
		NSString* outputName = [NSString stringWithUTF8String:outputName_.c_str()];
		MLMultiArray* values = [prediction featureValueForName:outputName].multiArrayValue;
		NSArray<NSNumber*>* shape = values.shape;
		if (values == nil || (shape.count != 3 && shape.count != 4) ||
			shape[0].integerValue != 1 ||
			(shape.count == 4 && shape[1].integerValue != 1)) {
			error = "Core ML depth model returned an invalid tensor.";
			return false;
		}
		const NSInteger outputHeight = shape[shape.count - 2].integerValue;
		const NSInteger outputWidth = shape[shape.count - 1].integerValue;
		if (outputWidth <= 0 || outputHeight <= 0 ||
			outputWidth > std::numeric_limits<int>::max() ||
			outputHeight > std::numeric_limits<int>::max() ||
			static_cast<size_t>(outputWidth) >
				std::numeric_limits<size_t>::max() / static_cast<size_t>(outputHeight)) {
			error = "Core ML depth model returned invalid dimensions.";
			return false;
		}
		width = static_cast<int>(outputWidth);
		height = static_cast<int>(outputHeight);
		output.resize(static_cast<size_t>(width) * height);
		const NSInteger strideY = values.strides[shape.count - 2].integerValue;
		const NSInteger strideX = values.strides[shape.count - 1].integerValue;
		if (strideY <= 0 || strideX <= 0) {
			error = "Core ML depth model returned invalid tensor strides.";
			return false;
		}
		if (values.dataType == MLMultiArrayDataTypeFloat16) {
			const auto* source = static_cast<const _Float16*>(values.dataPointer);
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
					output[static_cast<size_t>(y) * width + x] =
						static_cast<float>(source[y * strideY + x * strideX]);
		} else if (values.dataType == MLMultiArrayDataTypeFloat32) {
			const auto* source = static_cast<const float*>(values.dataPointer);
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
					output[static_cast<size_t>(y) * width + x] =
						source[y * strideY + x * strideX];
		} else {
			error = "Core ML depth model returned an unsupported tensor type.";
			return false;
		}
		return true;
	}
}
