#include "localfs.hpp"

#include "logging.hpp"
#include "stringutils.hpp"

namespace {
    FS_Archive mSdmc;
}

Result Archive::init(void)
{
    return FSUSER_OpenArchive(&mSdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
}

void Archive::exit(void)
{
    FSUSER_CloseArchive(mSdmc);
}

FS_Archive Archive::sdmc(void)
{
    return mSdmc;
}

Directory::Directory(FS_Archive archive, const std::u16string& root)
{
    mGood = false;
    mList.clear();
    Handle handle;

    mError = FSUSER_OpenDirectory(&handle, archive, fsMakePath(PATH_UTF16, root.data()));
    if (R_FAILED(mError)) {
        Logging::error("FSUSER_OpenDirectory failed with result 0x{:08X} for path {}", (unsigned)mError, StringUtils::UTF16toUTF8(root));
        return;
    }

    static constexpr u32 BATCH = 32;
    FS_DirectoryEntry batch[BATCH];
    u32 result;
    do {
        mError = FSDIR_Read(handle, &result, BATCH, batch);
        if (R_FAILED(mError)) {
            Logging::error("FSDIR_Read failed with result 0x{:08X} for path {}", (unsigned)mError, StringUtils::UTF16toUTF8(root));
            break;
        }
        for (u32 i = 0; i < result; i++) {
            mList.push_back(batch[i]);
        }
    } while (result == BATCH);

    Result readError = mError;
    mError           = FSDIR_Close(handle);
    if (R_FAILED(mError)) {
        Logging::error("FSDIR_Close failed with result 0x{:08X} for path {}", (unsigned)mError, StringUtils::UTF16toUTF8(root));
        mList.clear();
        return;
    }

    if (R_FAILED(readError)) {
        mError = readError;
        return;
    }

    mGood = true;
}

Result Directory::error(void)
{
    return mError;
}

bool Directory::good(void)
{
    return mGood;
}

std::u16string Directory::entry(size_t index)
{
    return index < mList.size() ? (char16_t*)mList.at(index).name : StringUtils::UTF8toUTF16("");
}

bool Directory::folder(size_t index)
{
    return index < mList.size() ? (mList.at(index).attributes & FS_ATTRIBUTE_DIRECTORY) != 0 : false;
}

size_t Directory::size(void)
{
    return mList.size();
}
