/*******************************************************************************
*
* MIT License
*
* Copyright (c) 2019 Advanced Micro Devices, Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*******************************************************************************/

#include <miopen/config.h>
#include <miopen/hip_build_utils.hpp>
#ifdef MIOPEN_LIBMLIRMIOPEN
#include <miopen/mlir_miopen_wrapper.hpp>
#endif
#include <miopen/stringutils.hpp>
#include <miopen/exec_utils.hpp>
#include <miopen/logger.hpp>
#include <miopen/env.hpp>
#include <miopen/target_properties.hpp>
#include <boost/optional.hpp>
#include <sstream>
#include <string>
#include <fstream>

MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEBUG_HIP_ENFORCE_COV3)
MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEBUG_HIP_VERBOSE)
MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEBUG_HIP_DUMP)

namespace miopen {

bool IsHccCompiler()
{
    static const auto isHcc = EndsWith(MIOPEN_HIP_COMPILER, "hcc");
    return isHcc;
}

bool IsHipClangCompiler()
{
    static const auto isClangXX = EndsWith(MIOPEN_HIP_COMPILER, "clang++");
    return isClangXX;
}

namespace {

inline bool ProduceCoV3()
{
    // If env.var is set, then let's follow it.
    if(IsEnabled(MIOPEN_DEBUG_HIP_ENFORCE_COV3{}))
        return true;
    if(IsDisabled(MIOPEN_DEBUG_HIP_ENFORCE_COV3{}))
        return false;
    // Otherwise, let's enable CO v3 for HIP kernels since ROCm 3.0.
    return (HipCompilerVersion() >= external_tool_version_t{3, 0, -1});
}

/// Returns option for enabling/disabling CO v3 generation for the compiler
/// that builds HIP kernels, depending on compiler version etc.
inline const std::string& GetCoV3Option(const bool enable)
{
    /// \note PR #2166 uses the "--hcc-cov3" option when isHCC is true.
    /// It's unclear why... HCC included in ROCm 2.8 does not support it,
    /// perhaps it suits for some older HCC?
    ///
    /// These options are Ok for ROCm 3.0:
    static const std::string option_enable{"-mcode-object-v3"};
    static const std::string no_option{};
    if(enable)
        return option_enable;
    else
        return no_option;
}
} // namespace

struct LcOptionTargetStrings
{
    const std::string& device;
    const std::string xnack;
    const std::string sramecc;
    LcOptionTargetStrings(const TargetProperties& target)
        : device(target.Name()),
          xnack(std::string{":xnack"} + (target.Xnack() ? "+" : "-")),
          sramecc(std::string{":sramecc"} + (target.Sramecc() ? "+" : "-"))
    {
    }
};

boost::filesystem::path HipBuild(boost::optional<TmpDir>& tmp_dir,
                                 const std::string& filename,
                                 std::string src,
                                 std::string params,
                                 const TargetProperties& target)
{
#ifdef __linux__
    MIOPEN_LOG_I("filename: " << filename);
    MIOPEN_LOG_I("src: " << src);
    MIOPEN_LOG_I("params: " << params);

    // write out the include files
    auto inc_list = GetKernelIncList();
    auto inc_path = tmp_dir->path;
    boost::filesystem::create_directories(inc_path);
    for(auto inc_file : inc_list)
    {
        auto inc_src = GetKernelInc(inc_file);
        WriteFile(inc_src, inc_path / inc_file);
    }

    // Invoke mlir kernel generator if filename has mlir in it
    if(filename.find("mlir_gen_igemm_conv2d_cpp") != std::string::npos)
    {
        // Should not have src content for mlir generated files
        assert(src.empty());

#ifdef MIOPEN_LIBMLIRMIOPEN
        MIOPEN_LOG_I("populating mlir igemm kernel");

        mlir::MlirHandle handle = mlir::CreateMlirHandle(params.c_str());

        // invoke mlir kernel generator.
        auto input_file       = tmp_dir->path / filename;
        auto input_file_base  = tmp_dir->path / input_file.stem();
        auto throw_if_invalid = [&filename](const std::string& gen_str) {
            if(gen_str.empty())
                MIOPEN_THROW(filename + " failed to build due to missing generated igemm string");
        };

        std::string source = mlir::MlirGenIgemmSource(handle);
        throw_if_invalid(source);
        std::ofstream source_file(input_file_base.string() + ".cpp");
        source_file << source;

        std::string header = mlir::MlirGenIgemmHeader(handle);
        throw_if_invalid(header);
        std::ofstream header_file(input_file_base.string() + ".hpp");
        header_file << header;

        // get mlir kernel compilation flags.
        std::string cflags = mlir::MlirGenIgemmCflags(handle);
        throw_if_invalid(cflags);
        mlir::DestroyMlirHandle(handle);

        // Skip first line.
        cflags = cflags.substr(cflags.find("\n") + 1);
        // Skip end of line.
        size_t pos = cflags.find("\n");
        if(pos != std::string::npos)
        {
            cflags.replace(pos, sizeof("\n"), " ");
        }

        params = cflags;

        MIOPEN_LOG_I("Finished populating mlir igemm kernel");
#endif
    }
    else
    {
        src += "\nint main() {}\n";
        WriteFile(src, tmp_dir->path / filename);
    }

    // cppcheck-suppress unreadVariable
    const LcOptionTargetStrings lots(target);

    auto env = std::string("");
    if(IsHccCompiler())
    {
        params += " -amdgpu-target=" + target.Name();
        params += " " + GetCoV3Option(ProduceCoV3());
    }
    else if(IsHipClangCompiler())
    {
        if(params.find("-std=") == std::string::npos)
            params += " --std=c++11";

        if(HipCompilerVersion() < external_tool_version_t{4, 0, 20482})
            params += " --cuda-gpu-arch=" + lots.device;
        else
            params += " --cuda-gpu-arch=" + lots.device + lots.xnack;

        params += " --cuda-device-only";
        params += " -c";
        params += " -O3 ";
    }

    params += " -Wno-unused-command-line-argument -I. ";
    params += MIOPEN_STRINGIZE(HIP_COMPILER_FLAGS);
    if(IsHccCompiler())
    {
        env += std::string("KMOPTLLC=\"-mattr=+enable-ds128 ");
        if(HipCompilerVersion() >= external_tool_version_t{2, 8, 0})
            env += " --amdgpu-spill-vgpr-to-agpr=0";
        env += '\"';
    }
    else if(IsHipClangCompiler())
    {
        params += " -mllvm --amdgpu-spill-vgpr-to-agpr=0";
    }

#if MIOPEN_BUILD_DEV
    if(miopen::IsEnabled(MIOPEN_DEBUG_HIP_VERBOSE{}))
    {
        params += " -v";
    }

    if(miopen::IsEnabled(MIOPEN_DEBUG_HIP_DUMP{}))
    {
        if(IsHccCompiler())
        {
            params += " -gline-tables-only";
            env += " KMDUMPISA=1";
            env += " KMDUMPLLVM=1";
        }
        else if(IsHipClangCompiler())
        {
            params += " -gline-tables-only";
            params += " -save-temps";
        }
    }
#endif

    // hip version
    params +=
        std::string(" -DHIP_PACKAGE_VERSION_FLAT=") + std::to_string(HIP_PACKAGE_VERSION_FLAT);

    params += " ";
    auto bin_file = tmp_dir->path / (filename + ".o");

    // compile
    tmp_dir->Execute(env + std::string(" ") + MIOPEN_HIP_COMPILER,
                     params + filename + " -o " + bin_file.string());
    if(!boost::filesystem::exists(bin_file))
        MIOPEN_THROW(filename + " failed to compile");
#ifdef EXTRACTKERNEL_BIN
    if(IsHccCompiler())
    {
        // call extract kernel
        tmp_dir->Execute(EXTRACTKERNEL_BIN, " -i " + bin_file.string());
        auto hsaco =
            std::find_if(boost::filesystem::directory_iterator{tmp_dir->path},
                         {},
                         [](auto entry) { return (entry.path().extension() == ".hsaco"); });

        if(hsaco == boost::filesystem::directory_iterator{})
        {
            MIOPEN_LOG_E("failed to find *.hsaco in " << hsaco->path().string());
        }

        return hsaco->path();
    }
    else
#endif
#ifdef MIOPEN_OFFLOADBUNDLER_BIN
        // clang-format off
    if(IsHipClangCompiler())
    {
        // clang-format on

        // call clang-offload-bundler
        tmp_dir->Execute(MIOPEN_OFFLOADBUNDLER_BIN,
                         "--type=o --targets=hip-amdgcn-amd-amdhsa-" +
                             (HipCompilerVersion() < external_tool_version_t{4, 0, 20482}
                                  ? lots.device
                                  : (std::string{'-'} + lots.device + lots.xnack)) +
                             " --inputs=" + bin_file.string() + " --outputs=" + bin_file.string() +
                             ".hsaco --unbundle");

        auto hsaco =
            std::find_if(boost::filesystem::directory_iterator{tmp_dir->path},
                         {},
                         [](auto entry) { return (entry.path().extension() == ".hsaco"); });

        if(hsaco == boost::filesystem::directory_iterator{})
        {
            MIOPEN_LOG_E("failed to find *.hsaco in " << hsaco->path().string());
        }
        return hsaco->path();
    }
    else
#endif
    {
        return bin_file;
    }
#else
    (void)filename;
    (void)params;
    MIOPEN_THROW("HIP kernels are only supported in Linux");
#endif
}

void bin_file_to_str(const boost::filesystem::path& file, std::string& buf)
{
    std::ifstream bin_file_ptr(file.string().c_str(), std::ios::binary);
    std::ostringstream bin_file_strm;
    bin_file_strm << bin_file_ptr.rdbuf();
    buf = bin_file_strm.str();
}

static external_tool_version_t HipCompilerVersionImpl()
{
    external_tool_version_t version;
    if(IsHccCompiler())
    {
        const std::string path(MIOPEN_HIP_COMPILER);
        const std::string mandatory_prefix("(based on HCC ");
        do
        {
            if(path.empty() || !std::ifstream(path).good())
                break;

            std::stringstream out;
            MIOPEN_LOG_NQI2("Running: " << '\'' << path << " --version" << '\'');
            if(miopen::exec::Run(path + " --version", nullptr, &out) != 0)
                break;

            std::string line;
            while(!out.eof())
            {
                std::getline(out, line);
                MIOPEN_LOG_NQI2(line);
                auto begin = line.find(mandatory_prefix);
                if(begin == std::string::npos)
                    continue;

                begin += mandatory_prefix.size();
                int v3, v2, v1 = v2 = v3 = -1;
                char c2, c1 = c2 = 'X';
                std::istringstream iss(line.substr(begin));
                iss >> v1 >> c1 >> v2 >> c2 >> v3;
                if(!iss.fail() && v1 >= 0)
                {
                    version.major = v1;
                    if(c1 == '.' && v2 >= 0)
                    {
                        version.minor = v2;
                        if(c2 == '.' && v3 >= 0)
                            version.patch = v3;
                    }
                }
                break;
            }
        } while(false);
    }
    else
    {
#ifdef HIP_PACKAGE_VERSION_MAJOR
        MIOPEN_LOG_NQI2("Read version information from HIP package...");
        version.major = HIP_PACKAGE_VERSION_MAJOR;
#ifdef HIP_PACKAGE_VERSION_MINOR
        version.minor = HIP_PACKAGE_VERSION_MINOR;
#else
        version.minor = 0;
#endif
#ifdef HIP_PACKAGE_VERSION_PATCH
        version.patch = HIP_PACKAGE_VERSION_PATCH;
#else
        version.patch = 0;
#endif
#else // HIP_PACKAGE_VERSION_MAJOR is not defined. CMake failed to find HIP package.
        MIOPEN_LOG_NQI2("...assuming 3.2.0 (hip-clang RC)");
        version.major = 3;
        version.minor = 2;
        version.patch = 0;
#endif
    }
    MIOPEN_LOG_NQI(version.major << '.' << version.minor << '.' << version.patch);
    return version;
}

external_tool_version_t HipCompilerVersion()
{
    static auto once = HipCompilerVersionImpl();
    return once;
}

bool operator>(const external_tool_version_t& lhs, const external_tool_version_t& rhs)
{
    if(lhs.major > rhs.major)
        return true;
    else if(lhs.major == rhs.major)
    {
        if(lhs.minor > rhs.minor)
            return true;
        else if(lhs.minor == rhs.minor)
            return (lhs.patch > rhs.patch);
        else
            return false;
    }
    else
        return false;
}

bool operator<(const external_tool_version_t& lhs, const external_tool_version_t& rhs)
{
    return rhs > lhs;
}
bool operator>=(const external_tool_version_t& lhs, const external_tool_version_t& rhs)
{
    return !(lhs < rhs);
}

bool operator<=(const external_tool_version_t& lhs, const external_tool_version_t& rhs)
{
    return !(lhs > rhs);
}

} // namespace miopen
