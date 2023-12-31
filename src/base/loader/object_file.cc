/*
 * Copyright (c) 2002-2004 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Nathan Binkert
 *          Steve Reinhardt
 */

#include "base/loader/object_file.hh"

#include <string>
#include <vector>

#include "base/loader/raw_image.hh"
#include "base/loader/symtab.hh"
#include "mem/port_proxy.hh"

using namespace std;

ObjectFile::ObjectFile(ImageFileDataPtr ifd) : ImageFile(ifd) {}

namespace {

typedef std::vector<ObjectFileFormat *> ObjectFileFormatList;

ObjectFileFormatList &
object_file_formats()
{
    static ObjectFileFormatList formats;
    return formats;
}

} // anonymous namespace

ObjectFileFormat::ObjectFileFormat()
{
    object_file_formats().emplace_back(this);
}

ObjectFile *
createObjectFile(const std::string &fname, bool raw)
{
    //printf("Test 0 in createObject of loader/object_file.cc with %s, %d\n", fname.c_str(), raw?0:1);
    ImageFileDataPtr ifd(new ImageFileData(fname));

    for (auto &format: object_file_formats()) {
        //printf("Test 1 in createObject of loader/object_file.cc\n");
        ObjectFile *file_obj = format->load(ifd);
        if (file_obj)
            return file_obj;
    }

    if (raw)
        return new RawImage(ifd);

    return nullptr;
}
