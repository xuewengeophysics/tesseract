///////////////////////////////////////////////////////////////////////
// File:        fontinfo.cpp
// Description: Font information classes abstracted from intproto.h/cpp.
// Author:      rays@google.com (Ray Smith)
// Created:     Wed May 18 10:39:01 PDT 2011
//
// (C) Copyright 2011, Google Inc.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////

#include "fontinfo.h"
#include "bitvector.h"
#include "unicity_table.h"

namespace tesseract {

// Writes to the given file. Returns false in case of error.
bool FontInfo::Serialize(FILE* fp) const {
  if (!write_info(fp, *this)) return false;
  if (!write_spacing_info(fp, *this)) return false;
  return true;
}
// Reads from the given file. Returns false in case of error.
// If swap is true, assumes a big/little-endian swap is needed.
bool FontInfo::DeSerialize(TFile* fp) {
  if (!read_info(fp, this)) return false;
  if (!read_spacing_info(fp, this)) return false;
  return true;
}

FontInfoTable::FontInfoTable() {
  set_compare_callback(NewPermanentTessCallback(CompareFontInfo));
  set_clear_callback(NewPermanentTessCallback(FontInfoDeleteCallback));
}

FontInfoTable::~FontInfoTable() {
}

// Writes to the given file. Returns false in case of error.
bool FontInfoTable::Serialize(FILE* fp) const {
  return this->SerializeClasses(fp);
}
// Reads from the given file. Returns false in case of error.
// If swap is true, assumes a big/little-endian swap is needed.
bool FontInfoTable::DeSerialize(TFile* fp) {
  truncate(0);
  return this->DeSerializeClasses(fp);
}

// Returns true if the given set of fonts includes one with the same
// properties as font_id.
bool FontInfoTable::SetContainsFontProperties(
    int font_id, const GenericVector<ScoredFont>& font_set) const {
  uint32_t properties = get(font_id).properties;
  for (int f = 0; f < font_set.size(); ++f) {
    if (get(font_set[f].fontinfo_id).properties == properties)
      return true;
  }
  return false;
}

// Returns true if the given set of fonts includes multiple properties.
bool FontInfoTable::SetContainsMultipleFontProperties(
    const GenericVector<ScoredFont>& font_set) const {
  if (font_set.empty()) return false;
  int first_font = font_set[0].fontinfo_id;
  uint32_t properties = get(first_font).properties;
  for (int f = 1; f < font_set.size(); ++f) {
    if (get(font_set[f].fontinfo_id).properties != properties)
      return true;
  }
  return false;
}

// Moves any non-empty FontSpacingInfo entries from other to this.
void FontInfoTable::MoveSpacingInfoFrom(FontInfoTable* other) {
  set_compare_callback(NewPermanentTessCallback(CompareFontInfo));
  set_clear_callback(NewPermanentTessCallback(FontInfoDeleteCallback));
  for (int i = 0; i < other->size(); ++i) {
    GenericVector<FontSpacingInfo*>* spacing_vec = other->get(i).spacing_vec;
    if (spacing_vec != nullptr) {
      int target_index = get_index(other->get(i));
      if (target_index < 0) {
        // Bit copy the FontInfo and steal all the pointers.
        push_back(other->get(i));
        other->get(i).name = nullptr;
      } else {
        delete [] get(target_index).spacing_vec;
        get(target_index).spacing_vec = other->get(i).spacing_vec;
      }
      other->get(i).spacing_vec = nullptr;
    }
  }
}

// Moves this to the target unicity table.
void FontInfoTable::MoveTo(UnicityTable<FontInfo>* target) {
  target->clear();
  target->set_compare_callback(NewPermanentTessCallback(CompareFontInfo));
  target->set_clear_callback(NewPermanentTessCallback(FontInfoDeleteCallback));
  for (int i = 0; i < size(); ++i) {
    // Bit copy the FontInfo and steal all the pointers.
    target->push_back(get(i));
    get(i).name = nullptr;
    get(i).spacing_vec = nullptr;
  }
}


// Compare FontInfo structures.
bool CompareFontInfo(const FontInfo& fi1, const FontInfo& fi2) {
  // The font properties are required to be the same for two font with the same
  // name, so there is no need to test them.
  // Consequently, querying the table with only its font name as information is
  // enough to retrieve its properties.
  return strcmp(fi1.name, fi2.name) == 0;
}
// Compare FontSet structures.
bool CompareFontSet(const FontSet& fs1, const FontSet& fs2) {
  if (fs1.size != fs2.size)
    return false;
  for (int i = 0; i < fs1.size; ++i) {
    if (fs1.configs[i] != fs2.configs[i])
      return false;
  }
  return true;
}

// Callbacks for GenericVector.
void FontInfoDeleteCallback(FontInfo f) {
  if (f.spacing_vec != nullptr) {
    f.spacing_vec->delete_data_pointers();
    delete f.spacing_vec;
  }
  delete[] f.name;
}
void FontSetDeleteCallback(FontSet fs) {
  delete[] fs.configs;
}

/*---------------------------------------------------------------------------*/
// Callbacks used by UnicityTable to read/write FontInfo/FontSet structures.
bool read_info(TFile* f, FontInfo* fi) {
  int32_t size;
  if (f->FReadEndian(&size, sizeof(size), 1) != 1) return false;
  char* font_name = new char[size + 1];
  fi->name = font_name;
  if (f->FRead(font_name, sizeof(*font_name), size) != size) return false;
  font_name[size] = '\0';
  if (f->FReadEndian(&fi->properties, sizeof(fi->properties), 1) != 1)
    return false;
  return true;
}

bool write_info(FILE* f, const FontInfo& fi) {
  int32_t size = strlen(fi.name);
  if (fwrite(&size, sizeof(size), 1, f) != 1) return false;
  if (static_cast<int>(fwrite(fi.name, sizeof(*fi.name), size, f)) != size)
    return false;
  if (fwrite(&fi.properties, sizeof(fi.properties), 1, f) != 1) return false;
  return true;
}

bool read_spacing_info(TFile* f, FontInfo* fi) {
  int32_t vec_size, kern_size;
  if (f->FReadEndian(&vec_size, sizeof(vec_size), 1) != 1) return false;
  ASSERT_HOST(vec_size >= 0);
  if (vec_size == 0) return true;
  fi->init_spacing(vec_size);
  for (int i = 0; i < vec_size; ++i) {
    FontSpacingInfo *fs = new FontSpacingInfo();
    if (f->FReadEndian(&fs->x_gap_before, sizeof(fs->x_gap_before), 1) != 1 ||
        f->FReadEndian(&fs->x_gap_after, sizeof(fs->x_gap_after), 1) != 1 ||
        f->FReadEndian(&kern_size, sizeof(kern_size), 1) != 1) {
      delete fs;
      return false;
    }
    if (kern_size < 0) {  // indication of a nullptr entry in fi->spacing_vec
      delete fs;
      continue;
    }
    if (kern_size > 0 && (!fs->kerned_unichar_ids.DeSerialize(f) ||
                          !fs->kerned_x_gaps.DeSerialize(f))) {
      delete fs;
      return false;
    }
    fi->add_spacing(i, fs);
  }
  return true;
}

bool write_spacing_info(FILE* f, const FontInfo& fi) {
  int32_t vec_size = (fi.spacing_vec == nullptr) ? 0 : fi.spacing_vec->size();
  if (fwrite(&vec_size,  sizeof(vec_size), 1, f) != 1) return false;
  int16_t x_gap_invalid = -1;
  for (int i = 0; i < vec_size; ++i) {
    FontSpacingInfo *fs = fi.spacing_vec->get(i);
    int32_t kern_size = (fs == nullptr) ? -1 : fs->kerned_x_gaps.size();
    if (fs == nullptr) {
      // Valid to have the identical fwrites. Writing invalid x-gaps.
      if (fwrite(&(x_gap_invalid), sizeof(x_gap_invalid), 1, f) != 1 ||
          fwrite(&(x_gap_invalid), sizeof(x_gap_invalid), 1, f) != 1 ||
          fwrite(&kern_size, sizeof(kern_size), 1, f) != 1) {
        return false;
      }
    } else {
      if (fwrite(&(fs->x_gap_before), sizeof(fs->x_gap_before), 1, f) != 1 ||
          fwrite(&(fs->x_gap_after), sizeof(fs->x_gap_after), 1, f) != 1 ||
          fwrite(&kern_size, sizeof(kern_size), 1, f) != 1) {
        return false;
      }
    }
    if (kern_size > 0 && (!fs->kerned_unichar_ids.Serialize(f) ||
                          !fs->kerned_x_gaps.Serialize(f))) {
      return false;
    }
  }
  return true;
}

bool read_set(TFile* f, FontSet* fs) {
  if (f->FReadEndian(&fs->size, sizeof(fs->size), 1) != 1) return false;
  fs->configs = new int[fs->size];
  if (f->FReadEndian(fs->configs, sizeof(fs->configs[0]), fs->size) != fs->size)
    return false;
  return true;
}

bool write_set(FILE* f, const FontSet& fs) {
  if (fwrite(&fs.size, sizeof(fs.size), 1, f) != 1) return false;
  for (int i = 0; i < fs.size; ++i) {
    if (fwrite(&fs.configs[i], sizeof(fs.configs[i]), 1, f) != 1) return false;
  }
  return true;
}

}  // namespace tesseract.
