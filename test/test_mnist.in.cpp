/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include <algorithm>
#include <assert.h>

#include "ngraph/ngraph.hpp"
#include "ngraph/pass/manager.hpp"
#include "ngraph/pass/visualize_tree.hpp"
#include "ngraph/util.hpp"

#include "util/all_close.hpp"
#include "util/ndarray.hpp"
#include "util/test_control.hpp"
#include "util/test_tools.hpp"

#include "he_backend.hpp"
#include "he_heaan_backend.hpp"
#include "he_seal_backend.hpp"

#include "test_util.hpp"

using namespace std;
using namespace ngraph;

static string s_manifest = "${MANIFEST}";

// ys is logits output, or one-hot encoded ground truth
vector<int> batched_argmax(const vector<float>& ys)
{
    if (ys.size() % 10 != 0)
    {
        cout << "ys.size() must be a multiple of 10" << endl;
        exit(1);
    }
    vector<int> labels;
    const float* data = ys.data();
    size_t idx = 0;
    while (idx < ys.size())
    {
        int label = distance(data + idx, max_element(data + idx, data + idx + 10));
        labels.push_back(label);
        idx += 10;
    }
    return labels;
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_no_batch)
{
    // Set batch_size = 1 for data loading
    // However, we don't create batched tensors
    size_t batch_size = 1;

    // We only support HEAAN backend for now
    auto backend = static_pointer_cast<runtime::he::he_heaan::HEHeaanBackend>(
        runtime::Backend::create("HE_HEAAN"));

    // vector<float> x = read_binary_constant(
    //     file_util::path_join(HE_SERIALIZED_ZOO, "weights/x_test_4096.bin"), batch_size * 784);
    vector<float> x(batch_size * 784, 0);
    vector<float> y = read_binary_constant(
        file_util::path_join(HE_SERIALIZED_ZOO, "weights/y_test_4096.bin"), batch_size * 10);

    // Global stop watch
    stopwatch sw_global;
    sw_global.start();

    // Load graph
    stopwatch sw_load_model;
    sw_load_model.start();
    const string filename = "mnist_cryptonets_batch_" + to_string(batch_size);
    const string json_path = file_util::path_join(HE_SERIALIZED_ZOO, filename + ".json");
    const string json_string = file_util::read_file_to_string(json_path);
    shared_ptr<Function> f = deserialize(json_string);
    NGRAPH_INFO << "Deserialize graph";
    NGRAPH_INFO << "x size " << x.size();
    NGRAPH_INFO << "Inputs loaded";
    sw_load_model.stop();
    NGRAPH_INFO << "sw_load_model: " << sw_load_model.get_milliseconds() << "ms";

    // Create input tensorview and copy tensors; create output tensorviews
    stopwatch sw_encrypt_input;
    sw_encrypt_input.start();
    auto parameters = f->get_parameters();
    vector<shared_ptr<runtime::TensorView>> parameter_tvs;
    for (auto parameter : parameters)
    {
        auto& shape = parameter->get_shape();
        auto& type = parameter->get_element_type();
        auto parameter_cipher_tv = backend->create_tensor(type, shape);
        NGRAPH_INFO << "Creating input shape: " << join(shape, "x");

        if (shape == Shape{batch_size, 784})
        {
            NGRAPH_INFO << "Copying " << shape_size(shape) << " elements";
            NGRAPH_INFO << "x is " << x.size() << " elements";
            copy_data(parameter_cipher_tv, x);
            parameter_tvs.push_back(parameter_cipher_tv);
        }
        else
        {
            NGRAPH_INFO << "Invalid shape" << join(shape, "x");
            throw ngraph_error("Invalid shape " + shape_size(shape));
        }
    }

    auto results = f->get_results();
    vector<shared_ptr<runtime::TensorView>> result_tvs;
    for (auto result : results)
    {
        auto& shape = result->get_shape();
        auto& type = result->get_element_type();
        NGRAPH_INFO << "Creating output shape: " << join(shape, "x");
        result_tvs.push_back(backend->create_tensor(type, shape));
    }
    sw_encrypt_input.stop();
    NGRAPH_INFO << "sw_encrypt_input: " << sw_encrypt_input.get_milliseconds() << "ms";

    // Run model
    NGRAPH_INFO << "calling function";
    stopwatch sw_run_model;
    sw_run_model.start();
    backend->call(f, result_tvs, parameter_tvs);
    sw_run_model.stop();
    NGRAPH_INFO << "sw_run_model: " << sw_run_model.get_milliseconds() << "ms";

    // Decrypt output
    stopwatch sw_decrypt_output;
    sw_decrypt_output.start();
    auto result = read_vector<float>(result_tvs[0]);
    sw_decrypt_output.stop();
    NGRAPH_INFO << "sw_decrypt_output: " << sw_decrypt_output.get_milliseconds() << "ms";

    // Stop global stop watch
    sw_global.stop();
    NGRAPH_INFO << "sw_global: " << sw_global.get_milliseconds() << "ms";

    // Check prediction vs ground truth
    vector<int> y_gt_label = batched_argmax(y);
    vector<int> y_predicted_label = batched_argmax(result);
    size_t error_count;
    for (size_t i = 0; i < y_gt_label.size(); ++i)
    {
        if (y_gt_label[i] != y_predicted_label[i])
        {
            // NGRAPH_INFO << "index " << i << " y_gt_label != y_predicted_label: " << y_gt_label[i]
            //             << " != " << y_predicted_label[i];
            error_count++;
        }
    }
    NGRAPH_INFO << "Accuracy: " << 1.f - (float)(error_count) / y.size();

    // Print results
    NGRAPH_INFO << "[Summary]";
    NGRAPH_INFO << "sw_load_model: " << sw_load_model.get_milliseconds() << "ms";
    NGRAPH_INFO << "sw_encrypt_input: " << sw_encrypt_input.get_milliseconds() << "ms";
    NGRAPH_INFO << "sw_run_model: " << sw_run_model.get_milliseconds() << "ms";
    NGRAPH_INFO << "sw_decrypt_output: " << sw_decrypt_output.get_milliseconds() << "ms";
    NGRAPH_INFO << "sw_global: " << sw_global.get_milliseconds() << "ms";
}

static void run_cryptonets_benchmark(size_t batch_size)
{
    // We only support HEAAN backend for now
    auto backend = static_pointer_cast<runtime::he::he_heaan::HEHeaanBackend>(
        runtime::Backend::create("HE_HEAAN"));

    vector<float> x = read_binary_constant(
        file_util::path_join(HE_SERIALIZED_ZOO, "weights/x_test_4096.bin"), batch_size * 784);
    vector<float> y = read_binary_constant(
        file_util::path_join(HE_SERIALIZED_ZOO, "weights/y_test_4096.bin"), batch_size * 10);

    // Global stop watch
    stopwatch sw_global;
    sw_global.start();

    // Load graph
    stopwatch sw_load_model;
    sw_load_model.start();
    const string filename = "mnist_cryptonets_batch_" + to_string(batch_size);
    const string json_path = file_util::path_join(HE_SERIALIZED_ZOO, filename + ".json");
    const string json_string = file_util::read_file_to_string(json_path);
    shared_ptr<Function> f = deserialize(json_string);
    NGRAPH_INFO << "Deserialize graph";
    NGRAPH_INFO << "x size " << x.size();
    NGRAPH_INFO << "Inputs loaded";
    sw_load_model.stop();
    NGRAPH_INFO << "sw_load_model: " << sw_load_model.get_milliseconds() << "ms";

    // Create input tensorview and copy tensors; create output tensorviews
    stopwatch sw_encrypt_input;
    sw_encrypt_input.start();
    auto parameters = f->get_parameters();
    vector<shared_ptr<runtime::TensorView>> parameter_tvs;
    for (auto parameter : parameters)
    {
        auto& shape = parameter->get_shape();
        auto& type = parameter->get_element_type();
        auto parameter_cipher_tv = backend->create_tensor(type, shape, true);
        NGRAPH_INFO << "Creating input shape: " << join(shape, "x");

        if (shape == Shape{batch_size, 784})
        {
            NGRAPH_INFO << "Copying " << shape_size(shape) << " elements";
            NGRAPH_INFO << "x is " << x.size() << " elements";
            copy_data(parameter_cipher_tv, x);
            parameter_tvs.push_back(parameter_cipher_tv);
        }
        else
        {
            NGRAPH_INFO << "Invalid shape" << join(shape, "x");
            throw ngraph_error("Invalid shape " + shape_size(shape));
        }
    }

    auto results = f->get_results();
    vector<shared_ptr<runtime::TensorView>> result_tvs;
    for (auto result : results)
    {
        auto& shape = result->get_shape();
        auto& type = result->get_element_type();
        NGRAPH_INFO << "Creating output shape: " << join(shape, "x");
        result_tvs.push_back(backend->create_tensor(type, shape, true));
    }
    sw_encrypt_input.stop();
    NGRAPH_INFO << "sw_encrypt_input: " << sw_encrypt_input.get_milliseconds() << "ms";

    // Run model
    NGRAPH_INFO << "calling function";
    stopwatch sw_run_model;
    sw_run_model.start();
    backend->call(f, result_tvs, parameter_tvs);
    sw_run_model.stop();
    NGRAPH_INFO << "sw_run_model: " << sw_run_model.get_milliseconds() << "ms";

    // Decrypt output
    stopwatch sw_decrypt_output;
    sw_decrypt_output.start();
    auto result = generalized_read_vector<float>(result_tvs[0]);
    sw_decrypt_output.stop();
    NGRAPH_INFO << "sw_decrypt_output: " << sw_decrypt_output.get_milliseconds() << "ms";

    // Stop global stop watch
    sw_global.stop();
    NGRAPH_INFO << "sw_global: " << sw_global.get_milliseconds() << "ms";

    // Check prediction vs ground truth
    vector<int> y_gt_label = batched_argmax(y);
    vector<int> y_predicted_label = batched_argmax(result);
    size_t error_count;
    for (size_t i = 0; i < y_gt_label.size(); ++i)
    {
        if (y_gt_label[i] != y_predicted_label[i])
        {
            // NGRAPH_INFO << "index " << i << " y_gt_label != y_predicted_label: " << y_gt_label[i]
            //             << " != " << y_predicted_label[i];
            error_count++;
        }
    }
    NGRAPH_INFO << "Accuracy: " << 1.f - (float)(error_count) / y.size();

    // Print results
    NGRAPH_INFO << "[Summary]";
    NGRAPH_INFO << "sw_load_model: " << sw_load_model.get_milliseconds() << "ms";
    NGRAPH_INFO << "sw_encrypt_input: " << sw_encrypt_input.get_milliseconds() << "ms";
    NGRAPH_INFO << "sw_run_model: " << sw_run_model.get_milliseconds() << "ms";
    NGRAPH_INFO << "sw_decrypt_output: " << sw_decrypt_output.get_milliseconds() << "ms";
    NGRAPH_INFO << "sw_global: " << sw_global.get_milliseconds() << "ms";
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_1)
{
    run_cryptonets_benchmark(1);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_2)
{
    run_cryptonets_benchmark(2);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_4)
{
    run_cryptonets_benchmark(4);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_8)
{
    run_cryptonets_benchmark(8);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_16)
{
    run_cryptonets_benchmark(16);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_32)
{
    run_cryptonets_benchmark(32);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_64)
{
    run_cryptonets_benchmark(64);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_128)
{
    run_cryptonets_benchmark(128);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_256)
{
    run_cryptonets_benchmark(256);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_512)
{
    run_cryptonets_benchmark(512);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_1024)
{
    run_cryptonets_benchmark(1024);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_2048)
{
    run_cryptonets_benchmark(2048);
}

NGRAPH_TEST(HE_HEAAN, cryptonets_benchmark_4096)
{
    run_cryptonets_benchmark(4096);
}

NGRAPH_TEST(${BACKEND_NAME}, tf_mnist_cryptonets_batch)
{
    auto backend = static_pointer_cast<runtime::he::he_heaan::HEHeaanBackend>(
        runtime::Backend::create("${BACKEND_NAME}"));
    // auto backend = runtime::Backend::create("INTERPRETER");

    size_t batch_size = 4096;

    NGRAPH_INFO << "Loaded backend";
    const string filename = "mnist_cryptonets_batch_" + to_string(batch_size);
    const string json_path = file_util::path_join(HE_SERIALIZED_ZOO, filename + ".js");
    const string json_string = file_util::read_file_to_string(json_path);
    shared_ptr<Function> f = deserialize(json_string);

    // Visualize model
    auto model_file_name = filename + string(".") + pass::VisualizeTree::get_file_ext();
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::VisualizeTree>(model_file_name);
    pass_manager.run_passes(f);
    NGRAPH_INFO << "Saved file " << model_file_name;

    vector<float> x = read_binary_constant(
        file_util::path_join(HE_SERIALIZED_ZOO, "weights/x_test_" + to_string(batch_size) + ".bin"),
        batch_size * 784);
    vector<float> y = read_binary_constant(
        file_util::path_join(HE_SERIALIZED_ZOO, "weights/y_test_" + to_string(batch_size) + ".bin"),
        batch_size * 10);
    vector<float> cpu_result = read_binary_constant(
        file_util::path_join(HE_SERIALIZED_ZOO,
                             "weights/cpu_result_" + to_string(batch_size) + ".bin"),
        batch_size * 10);
    NGRAPH_INFO << "cpu_result size " << cpu_result.size();
    NGRAPH_INFO << "x size " << x.size();
    NGRAPH_INFO << "y size " << y.size();

    NGRAPH_INFO << "Deserialized graph";
    auto parameters = f->get_parameters();
    vector<shared_ptr<runtime::TensorView>> parameter_tvs;
    for (auto parameter : parameters)
    {
        auto& shape = parameter->get_shape();
        auto& type = parameter->get_element_type();
        auto parameter_cipher_tv = backend->create_tensor(type, shape, true);
        // Uncomment for INTERPRETER backend
        // auto parameter_cipher_tv = backend->create_tensor(type, shape);

        NGRAPH_INFO << join(shape, "x");

        if (shape == Shape{batch_size, 784})
        {
            NGRAPH_INFO << "Copying " << shape_size(shape) << " elements";
            NGRAPH_INFO << "x is " << x.size() << " elements";
            copy_data(parameter_cipher_tv, x);
            parameter_tvs.push_back(parameter_cipher_tv);
        }
        else
        {
            NGRAPH_INFO << "Invalid shape" << join(shape, "x");
            throw ngraph_error("Invalid shape " + shape_size(shape));
        }
    }

    auto results = f->get_results();
    vector<shared_ptr<runtime::TensorView>> result_tvs;
    for (auto result : results)
    {
        auto& shape = result->get_shape();
        auto& type = result->get_element_type();
        NGRAPH_INFO << "Creating batched result tensor shape ";
        for (auto elem : shape)
        {
            NGRAPH_INFO << elem;
        }

        // Uncomment for interpreter backend
        // result_tvs.push_back(backend->create_tensor(type, shape));
        result_tvs.push_back(backend->create_tensor(type, shape, true));
    }

    NGRAPH_INFO << "calling function";
    backend->call(f, result_tvs, parameter_tvs);

    auto result = generalized_read_vector<float>(result_tvs[0]);

    if (false) // Set to true to save result from interpreter backend
    {
        write_binary_constant(
            result,
            file_util::path_join(HE_SERIALIZED_ZOO,
                                 "weights/cpu_result_" + to_string(batch_size) + ".bin"));
    }

    float accuracy = get_accuracy(result, y);
    NGRAPH_INFO << "Accuracy " << accuracy;

    EXPECT_TRUE(test::all_close(cpu_result, result, 1e-5f, 4e-3f));
}

NGRAPH_TEST(${BACKEND_NAME}, tf_mnist_softmax_quantized_1)
{
    auto backend = static_pointer_cast<runtime::he::he_heaan::HEHeaanBackend>(
        runtime::Backend::create("${BACKEND_NAME}"));

    NGRAPH_INFO << "Loaded backend";
    const string json_path = file_util::path_join(HE_SERIALIZED_ZOO, "mnist_mlp_const_1_inputs.js");
    const string json_string = file_util::read_file_to_string(json_path);
    shared_ptr<Function> f = deserialize(json_string);

    NGRAPH_INFO << "Deserialized graph";
    auto parameters = f->get_parameters();
    vector<shared_ptr<runtime::TensorView>> parameter_tvs;
    for (auto parameter : parameters)
    {
        auto& shape = parameter->get_shape();
        auto& type = parameter->get_element_type();
        NGRAPH_INFO << "Creating tensor of " << shape_size(shape) << " elements";
        auto parameter_tv = backend->create_tensor(type, shape);
        NGRAPH_INFO << "created tensor of " << shape_size(shape) << " elements";
        copy_data(parameter_tv, vector<float>(shape_size(shape)));
        parameter_tvs.push_back(parameter_tv);
    }

    auto results = f->get_results();
    vector<shared_ptr<runtime::TensorView>> result_tvs;
    for (auto result : results)
    {
        auto& shape = result->get_shape();
        auto& type = result->get_element_type();
        result_tvs.push_back(backend->create_tensor(type, shape));
    }

    NGRAPH_INFO << "calling function ";
    backend->call(f, result_tvs, parameter_tvs);

    EXPECT_EQ((vector<float>{2173, 944, 1151, 1723, -1674, 569, -1985, 9776, -4997, -1903}),
              read_vector<float>(result_tvs[0]));
}
