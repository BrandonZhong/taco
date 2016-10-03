#include "tensor.h"

#include "packed_tensor.h"
#include "format.h"
#include "tree.h"

using namespace std;

namespace taco {

typedef PackedTensor::IndexType  IndexType;
typedef PackedTensor::IndexArray IndexArray;
typedef PackedTensor::Index      Index;
typedef PackedTensor::Indices    Indices;

/// Count unique entries between iterators (assumes values are sorted)
static vector<int> getUniqueEntries(const vector<int>::const_iterator& begin,
                                    const vector<int>::const_iterator& end) {
  vector<int> uniqueEntries;
  if (begin != end) {
    size_t curr = *begin;
    uniqueEntries.push_back(curr);
    for (auto it = begin+1; it != end; ++it) {
      size_t next = *it;
      iassert(next >= curr);
      if (curr < next) {
        curr = next;
        uniqueEntries.push_back(curr);
      }
    }
  }
  return uniqueEntries;
}

static void packIndices(const vector<size_t>& dims,
                        const vector<vector<int>>& coords,
                        size_t begin, size_t end,
                        const vector<Level>& levels, size_t i,
                        Indices* indices) {

  auto& level       = levels[i];
  auto& levelCoords = coords[i];
  auto& index       = (*indices)[i];

  switch (level.type) {
    case Level::Dense: {
      // Iterate over each index value and recursively pack it's segment
      size_t cbegin = begin;
      for (int j=0; j < (int)dims[i]; ++j) {
        // Scan to find segment range of children
        size_t cend = cbegin;
        while (cend < end && levelCoords[cend] == j) {
          cend++;
        }
        packIndices(dims, coords, cbegin, cend, levels, i+1, indices);
        cbegin = cend;
      }
      break;
    }
    case Level::Sparse: {
      auto indexValues = getUniqueEntries(levelCoords.begin()+begin,
                                          levelCoords.begin()+end);

      // Store segment end: the size of the stored segment is the number of
      // unique values in the coordinate list
      index[0].push_back(index[1].size() + indexValues.size());

      // Store unique index values for this segment
      index[1].insert(index[1].end(), indexValues.begin(), indexValues.end());

      // Iterate over each index value and recursively pack it's segment
      size_t cbegin = begin;
      for (int j : indexValues) {
        // Scan to find segment range of children
        size_t cend = cbegin;
        while (cend < end && levelCoords[cend] == j) {
          cend++;
        }
        packIndices(dims, coords, cbegin, cend, levels, i+1, indices);
        cbegin = cend;
      }
      break;
    }
    case Level::Values: {
      // Do nothing
      break;
    }
  }
}

shared_ptr<PackedTensor>
pack(const vector<size_t>& dimensions, internal::ComponentType ctype,
     const Format& format, const vector<vector<int>>& coords,
     const void* vals) {
  iassert(coords.size() > 0);
  size_t numCoords = coords[0].size();

  const vector<Level>& levels = format.getLevels();
  Indices indices;
  indices.reserve(levels.size()-1);

  // Create the vectors to store pointers to indices/index sizes
  size_t nnz = 1;
  for (size_t i=0; i < levels.size(); ++i) {
    auto& level = levels[i];
    switch (level.type) {
      case Level::Dense: {
        indices.push_back({});
        nnz *= dimensions[i];
        break;
      }
      case Level::Sparse: {
        // A sparse level packs nnz down to #coords
        nnz = numCoords;

        // Sparse indices have two arrays: a segment array and an index array
        indices.push_back({{}, {}});

        // Add start of first segment
        indices[i][0].push_back(0);
        break;
      }
      case Level::Values: {
        // Do nothing
        break;
      }
    }
  }

  std::cout << "coordinate arrays:" << std::endl;
  for (auto& coord : coords) {
    std::cout << "  " << util::join(coord) << std::endl;
  }
  std::cout << std::endl;

  // Pack indices
  packIndices(dimensions, coords, 0,numCoords,levels, 0, &indices);

  // Print indices
  for (size_t i=0; i < indices.size(); ++i) {
    auto& index = indices[i];
    std::cout << "index" << std::endl;
    for (size_t j=0; j < index.size(); ++j) {
      auto& indexArray = index[j];
      std::cout << "  {" << util::join(indexArray) << "}" << std::endl;
    }
  }

  // Pack values
  tassert(ctype == internal::ComponentType::Double)
      << "make the packing machinery work with other primitive types later. "
      << "Right now we're specializing to doubles so that we can use a "
      << "resizable std::vector, but eventually we should use a two pass pack "
      << "algorithm that figures out sizes first, and then packs the data";

  //  std::cout << "nnz: " << nnz << std::endl;
  std::vector<double> values(nnz);

  // Print values
  std::cout << "values" << std::endl
            << "  {" << util::join(values) << "}" << std::endl;

  return make_shared<PackedTensor>(nnz, values, indices);
}

}
