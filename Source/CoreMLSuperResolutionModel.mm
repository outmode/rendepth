// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "CoreMLSuperResolutionModel.h"

#import <CoreML/CoreML.h>

#include <cstring>
#include <limits>

namespace {
std::string message(NSError* error) {
	return error == nil ? "Unknown Core ML error." : error.localizedDescription.UTF8String;
}
}

std::unique_ptr<CoreMLSuperResolutionModel> CoreMLSuperResolutionModel::load(
	const std::filesystem::path& path, std::string& error) {
	@autoreleasepool {
		error.clear();
		if (!std::filesystem::is_directory(path)) {
			error = "Compiled Core ML super-resolution model was not found: " + path.string();
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
		if (input.type != MLFeatureTypeMultiArray ||
			input.multiArrayConstraint.shape.count != 4 ||
			input.multiArrayConstraint.shape[0].integerValue != 1 ||
			input.multiArrayConstraint.shape[1].integerValue != 3 ||
			model.modelDescription.outputDescriptionsByName.count != 1) {
			error = "Core ML super-resolution model has unexpected inputs or outputs.";
			return nullptr;
		}
		auto result = std::unique_ptr<CoreMLSuperResolutionModel>(
			new CoreMLSuperResolutionModel());
		result->model_ = CFBridgingRetain(model);
		result->outputName_ =
			model.modelDescription.outputDescriptionsByName.allKeys.firstObject.UTF8String;
		return result;
	}
}

CoreMLSuperResolutionModel::~CoreMLSuperResolutionModel() {
	if (model_ != nullptr) CFBridgingRelease(model_);
}

bool CoreMLSuperResolutionModel::predict(const float* input, int width, int height,
	std::vector<float>& output, int& outputWidth, int& outputHeight,
	std::string& error) const {
	@autoreleasepool {
		error.clear();
		if (input == nullptr || width < 16 || height < 16 || width > 720 || height > 720) {
			error = "Core ML super-resolution input must be 16–720 pixels per side.";
			return false;
		}
		NSError* nativeError = nil;
		MLMultiArray* array = [[MLMultiArray alloc] initWithShape:@[@1, @3,
			@(height), @(width)] dataType:MLMultiArrayDataTypeFloat32 error:&nativeError];
		if (array == nil) {
			error = message(nativeError);
			return false;
		}
		std::memcpy(array.dataPointer, input,
			static_cast<size_t>(width) * height * 3 * sizeof(float));
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
		if (values == nil || shape.count != 4 || shape[0].integerValue != 1 ||
			shape[1].integerValue != 3 || shape[2].integerValue != height * 4 ||
			shape[3].integerValue != width * 4) {
			error = "Core ML super-resolution model returned an invalid tensor.";
			return false;
		}
		outputHeight = static_cast<int>(shape[2].integerValue);
		outputWidth = static_cast<int>(shape[3].integerValue);
		const size_t plane = static_cast<size_t>(outputWidth) * outputHeight;
		if (plane > std::numeric_limits<size_t>::max() / 3) {
			error = "Core ML super-resolution output is too large.";
			return false;
		}
		const NSInteger strideC = values.strides[1].integerValue;
		const NSInteger strideY = values.strides[2].integerValue;
		const NSInteger strideX = values.strides[3].integerValue;
		if (strideC <= 0 || strideY <= 0 || strideX <= 0) {
			error = "Core ML super-resolution output has invalid strides.";
			return false;
		}
		output.resize(plane * 3);
		if (values.dataType == MLMultiArrayDataTypeFloat16) {
			const auto* source = static_cast<const _Float16*>(values.dataPointer);
			for (int c = 0; c < 3; ++c)
				for (int y = 0; y < outputHeight; ++y)
					for (int x = 0; x < outputWidth; ++x)
						output[static_cast<size_t>(c) * plane + static_cast<size_t>(y) * outputWidth + x] =
							static_cast<float>(source[c * strideC + y * strideY + x * strideX]);
		} else if (values.dataType == MLMultiArrayDataTypeFloat32) {
			const auto* source = static_cast<const float*>(values.dataPointer);
			for (int c = 0; c < 3; ++c)
				for (int y = 0; y < outputHeight; ++y)
					for (int x = 0; x < outputWidth; ++x)
						output[static_cast<size_t>(c) * plane + static_cast<size_t>(y) * outputWidth + x] =
							source[c * strideC + y * strideY + x * strideX];
		} else {
			error = "Core ML super-resolution output has an unsupported tensor type.";
			return false;
		}
		return true;
	}
}
