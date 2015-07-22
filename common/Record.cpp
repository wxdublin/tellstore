#include <tellstore/Record.hpp>

#include <crossbow/enum_underlying.hpp>

namespace tell {
namespace store {


Field::Field(Field&& other)
    : FieldBase(other.mType)
    , mName(std::move(other.mName))
    , mNotNull(other.mNotNull)
    , mData(other.mData)
{}

Field::Field(const Field& other)
    : FieldBase(other.mType)
    , mName(other.mName)
    , mNotNull(other.mNotNull)
    , mData(other.mData)
{}

Field& Field::operator=(Field&& other)
{
    mType = other.mType;
    mName = std::move(other.mName);
    mNotNull = other.mNotNull;
    mData = other.mData;
    return *this;
}

Field& Field::operator=(const Field& other)
{
    mType = other.mType;
    mName = other.mName;
    mNotNull = other.mNotNull;
    mData = other.mData;
    return *this;
}


size_t Field::defaultSize() const {
    if (isFixedSized()) return staticSize();
    switch (mType) {
        case FieldType::NULLTYPE:
            LOG_ERROR("NULLTYPE is not appropriate to use in a schema");
            return 0;
        case FieldType::TEXT:
            return sizeof(uint32_t);
        case FieldType::BLOB:
            return sizeof(uint32_t);
        case FieldType::NOTYPE:
            assert(false);
            LOG_ERROR("One should never use a field of type NOTYPE");
            return std::numeric_limits<size_t>::max();
        default:
            assert(false);
            LOG_ERROR("Unknown type");
            return 0;
    }
}

size_t Field::sizeOf(const boost::any& value) const {
    if (isFixedSized()) return staticSize();
    switch (mType) {
        case FieldType::NULLTYPE:
            LOG_ERROR("NULLTYPE is not appropriate to use in a schema");
            return 0;
        case FieldType::TEXT:
        case FieldType::BLOB:
            return sizeof(uint32_t) + boost::any_cast<crossbow::string>(value).size();
        case FieldType::NOTYPE:
            assert(false);
            LOG_ERROR("One should never use a field of type NOTYPE");
            return std::numeric_limits<size_t>::max();
        default:
            assert(false);
            LOG_ERROR("Unknown type");
            return 0;
    }
}

bool Schema::addField(FieldType type, const crossbow::string& name, bool notNull) {
    if (name.size() > std::numeric_limits<uint16_t>::max()) {
        LOG_DEBUG("Field name with %d bytes are not supported", name.size());
        return false;
    }
    if (mFixedSizeFields.size() + mVarSizeFields.size() + 1 > std::numeric_limits<uint16_t>::max()) {
        LOG_ERROR("%d is the maximum number of columns in a table", std::numeric_limits<uint16_t>::max());
        return false;
    }
    bool res = true;
    for (auto iter = mFixedSizeFields.begin(); iter != mFixedSizeFields.end() && res; ++iter) {
        res = iter->name() != name;
    }
    for (auto iter = mVarSizeFields.begin(); iter != mVarSizeFields.end() && res; ++iter) {
        res = iter->name() != name;
    }
    if (!res) {
        LOG_TRACE("Tried to insert an already existing field: %s", name);
        return res;
    }
    mAllNotNull &= notNull;
    Field f(type, name, notNull);
    if (f.isFixedSized()) {
        mFixedSizeFields.emplace_back(f);
    } else {
        mVarSizeFields.emplace_back(f);
    }
    return true;
}

size_t Schema::schemaSize() const {
    size_t res = 8;
    // TODO Fix the alignment
    for (auto& field: mFixedSizeFields) {
        res += 5;
        res += field.name().size();
    }
    for (auto& field: mVarSizeFields) {
        res += 5;
        res += field.name().size();
    }
    return res;
}

namespace {

inline char* serialize_field(const Field& field, char* ptr) {
    uint16_t fieldType = crossbow::to_underlying(field.type());
    memcpy(ptr, &fieldType, sizeof(fieldType));
    ptr += sizeof(fieldType);
    bool isNotNull = field.isNotNull();
    memcpy(ptr, &isNotNull, sizeof(isNotNull));
    ptr += sizeof(isNotNull);
    const crossbow::string& name = field.name();
    uint16_t nameSize = uint16_t(name.size());
    memcpy(ptr, &nameSize, sizeof(nameSize));
    ptr += sizeof(nameSize);
    memcpy(ptr, name.data(), name.size());
    ptr += name.size();
    return ptr;
}

} // namespace {}

char* Schema::serialize(char* ptr) const {
    uint32_t sz = uint32_t(schemaSize());
    memcpy(ptr, &sz, sizeof(sz));
    ptr += sizeof(sz);
    uint16_t numColumns = uint16_t(mFixedSizeFields.size() + mVarSizeFields.size());
    memcpy(ptr, &numColumns, sizeof(numColumns));
    ptr += sizeof(numColumns);
    uint8_t type = static_cast<uint8_t>(mType);
    memcpy(ptr, &type, sizeof(type));
    ptr += sizeof(type);
    uint8_t allNotNull = uint8_t(mAllNotNull ? 1 : 0);
    memcpy(ptr, &allNotNull, sizeof(allNotNull));
    ptr += sizeof(allNotNull);
    for (auto& field : mFixedSizeFields) {
        ptr = serialize_field(field, ptr);
    }
    for (auto& field : mVarSizeFields) {
        ptr = serialize_field(field, ptr);
    }
    return ptr;
}

Schema::Schema(const char* ptr) {
    // we can ignore the size
    ptr += sizeof(uint32_t);
    uint16_t numColumns = *reinterpret_cast<const uint16_t*>(ptr);
    ptr += sizeof(numColumns);
    uint8_t type = *reinterpret_cast<const uint8_t*>(ptr);
    mType = crossbow::from_underlying<TableType>(type);
    ptr += sizeof(type);
    uint8_t allNotNull = *reinterpret_cast<const uint8_t*>(ptr);
    mAllNotNull = allNotNull > 0;
    ptr += sizeof(allNotNull);
    for (uint16_t i = 0; i < numColumns; ++i) {
        FieldType type = *reinterpret_cast<const FieldType*>(ptr);
        ptr += sizeof(type);
        bool isNotNull = *reinterpret_cast<const bool*>(ptr);
        ptr += sizeof(isNotNull);
        uint16_t nameSize = *reinterpret_cast<const uint16_t*>(ptr);
        ptr += sizeof(nameSize);
        Field f(type, crossbow::string(ptr, nameSize), isNotNull);
        ptr += nameSize;
        if (f.isFixedSized()) {
            mFixedSizeFields.emplace_back(std::move(f));
        } else {
            mVarSizeFields.emplace_back(std::move(f));
        }
    }
}

Record::Record(const Schema& schema)
    : mSchema(schema), mFieldMetaData(schema.fixedSizeFields().size() + schema.varSizeFields().size()) {
    int32_t currOffset = mSchema.allNotNull() ? 0 : (mFieldMetaData.size() + 7)/8;
    currOffset += (currOffset % 8) ? 8 - (currOffset % 8) : 0;

    size_t id = 0;
    for (const auto& field : schema.fixedSizeFields()) {
        mIdMap.insert(std::make_pair(field.name(), id));
        mFieldMetaData[id++] = std::make_pair(field, currOffset);
        currOffset += field.staticSize();
    }
    for (const auto& field : schema.varSizeFields()) {
        mIdMap.insert(std::make_pair(field.name(), id));
        mFieldMetaData[id++] = std::make_pair(field, currOffset);
        // make sure, that all others are set to min
        currOffset = std::numeric_limits<int32_t>::min();
    }
}

size_t Record::sizeOfTuple(const GenericTuple& tuple) const
{
    size_t result = mSchema.allNotNull() ? 0 : (mFieldMetaData.size() + 7)/8;
    result += (result % 8) ? 8 - (result % 8) : 0;
    for (auto& f : mFieldMetaData) {
        auto& field = f.first;
        if (field.isFixedSized()) {
            result += field.staticSize();
            // we will need this space anyway
            // no matter what tuple contains
            continue;
        }
        const auto& name = field.name();
        auto iter = tuple.find(name);
        if (iter == tuple.end()) {
            // In this case we either set the value
            // to NULL (if possible), or to the
            // default value
            if (field.isNotNull()) {
                result += field.defaultSize();
            }
            continue;
        } else {
            result += field.sizeOf(iter->second);
        }
    }
    // we have to make sure that the size of a tuple is 8 byte aligned
    result += (result % 8) ? 8 - (result % 8) : 0;
    return result;
}

size_t Record::sizeOfTuple(const char* ptr) const {
    auto baseId = mSchema.fixedSizeFields().size();
    auto pos = mFieldMetaData[baseId].second;
    LOG_ASSERT(pos > 0, "Offset for first variable length field is smaller than 0");

    for (; baseId < mFieldMetaData.size(); ++baseId) {
        // we know, that now all fields are variable mLength - that means the first four bytes are always the
        // field size
        pos += *reinterpret_cast<const uint32_t* const>(ptr + pos) + sizeof(uint32_t);
    }
    // we have to make sure that the size of a tuple is 8 byte aligned
    pos += (pos % 8) ? 8 - (pos % 8) : 0;
    return pos;
}


char* Record::create(const GenericTuple& tuple, size_t& size) const {
    uint32_t recSize = uint32_t(sizeOfTuple(tuple));
    size = size_t(recSize);
    std::unique_ptr<char[]> result(new char[recSize]);
    if (!create(result.get(), tuple, recSize)) {
        return nullptr;
    }
    return result.release();
}

char* Record::create(char* result, const GenericTuple& tuple, uint32_t recSize) const {
    LOG_ASSERT(recSize == sizeOfTuple(tuple), "Size has to be the actual tuple size");
    using uchar = unsigned char;
    char* res = result;
    auto headerSize = mSchema.allNotNull() ? 0 : (mFieldMetaData.size() + 7)/8;
    headerSize += (headerSize % 8) ? (8 - (headerSize % 8)) : 0;
    char* current = res + headerSize;
    memset(res, 0, recSize);
    for (id_t id = 0; id < mFieldMetaData.size(); ++id) {
        auto& f = mFieldMetaData[id];
        const auto& name = f.first.name();
        // first we need to check whether the value for this field is
        // given
        auto iter = tuple.find(name);
        if (iter == tuple.end()) {
            // in this case, we set it to NULL or to
            // the default value. This might be a wrong
            // behavior, but the processing layer is
            // responsible for catching errors of this
            // kind.
            if (f.first.isNotNull()) {
                // Therefore we set it to the default value
                // In all cases, this will be represnted
                // by setting all to 0 - which is already
                // done in the memset at the beginning of this
                // function.
            } else {
                // In this case we set the field to NULL
                uchar& bitmap = *reinterpret_cast<uchar*>(res + id / 8);
                uchar pos = uchar(id % 8);
                bitmap |= uchar(0x1 << pos);
            }
            // after writing the data back, we might need to increase
            // the current pointer
            if (f.first.isFixedSized() || !f.first.isNotNull()) {
                // We store the information, even if the field is
                // null. This is for performance reason (it makes it
                // faster to access any fixed-size fields).
                // If the field was declared not null, than we have
                // to write a tuple with its default value
                switch (f.first.type()) {
                case FieldType::NOTYPE:
                    LOG_ERROR("notype not allowed here");
                    std::terminate();
                    break;
                case FieldType::NULLTYPE:
                    LOG_ERROR("null not allowed here");
                    std::terminate();
                    break;
                case FieldType::SMALLINT:
                    current += sizeof(int16_t);
                    break;
                case FieldType::INT:
                    current += sizeof(int32_t);
                    break;
                case FieldType::BIGINT:
                    current += sizeof(int64_t);
                    break;
                case FieldType::FLOAT:
                    current += sizeof(float);
                    break;
                case FieldType::DOUBLE:
                    current += sizeof(double);
                    break;
                case FieldType::TEXT:
                case FieldType::BLOB:
                    LOG_ERROR("TEXT/BLOB can not be fixed size");
                    assert(false);
                }
            }
        } else if (f.first.isFixedSized()) {
            // we just need to copy the value to the correct offset.
            auto offset = f.second;
            size_t s = 0;
            switch (f.first.type()) {
            case FieldType::NOTYPE:
                LOG_ERROR("Try to write something with no type");
                return nullptr;
            case FieldType::NULLTYPE:
                LOG_ERROR("NULLTYPE is not allowed here");
                return nullptr;
            case FieldType::SMALLINT:
                s = sizeof(int16_t);
                memcpy(res + offset, boost::any_cast<int16_t>(&(iter->second)), s);
                break;
            case FieldType::INT:
                s = sizeof(int32_t);
                memcpy(res + offset, boost::any_cast<int32_t>(&(iter->second)), s);
                break;
            case FieldType::BIGINT:
                s = sizeof(int64_t);
                memcpy(res + offset, boost::any_cast<int64_t>(&(iter->second)), s);
                break;
            case FieldType::FLOAT:
                s = sizeof(float);
                memcpy(res + offset, boost::any_cast<float>(&(iter->second)), s);
                break;
            case FieldType::DOUBLE:
                s = sizeof(double);
                memcpy(res + offset, boost::any_cast<double>(&(iter->second)), s);
                break;
            case FieldType::TEXT:
            case FieldType::BLOB:
                LOG_ERROR("This should never happen");
                std::terminate();
                break;
            }
            current += s;
        } else {
            // The value type is not fixed sized.
            switch (f.first.type()) {
            case FieldType::NOTYPE:
                LOG_ERROR("Try to write something with no type");
                return nullptr;
            case FieldType::NULLTYPE:
                LOG_ERROR("NULLTYPE is not allowed here");
                return nullptr;
            case FieldType::SMALLINT:
            case FieldType::INT:
            case FieldType::BIGINT:
            case FieldType::FLOAT:
            case FieldType::DOUBLE:
                LOG_ERROR("This should never happen");
                std::terminate();
                return nullptr;
            case FieldType::TEXT:
            case FieldType::BLOB:
                {
                    const crossbow::string& str = *boost::any_cast<crossbow::string>(&(iter->second));
                    uint32_t len = uint32_t(str.size());
                    memcpy(current, &len, sizeof(len));
                    current += sizeof(len);
                    memcpy(current, str.c_str(), len);
                    current += len;
                }
                break;
            }
        }
    }
    return result;
}

const char* Record::data(const char* const ptr, Record::id_t id, bool& isNull, FieldType* type /* = nullptr*/) const {
    if (id >= mFieldMetaData.size()) {
        LOG_ERROR("Tried to get nonexistent id");
        assert(false);
        return nullptr;
    }
    using uchar = unsigned char;
    isNull = false;
    if (!mSchema.allNotNull()) {
        // TODO: Check whether the compiler optimizes this correctly - otherwise this might be inefficient (but more readable)
        uchar bitmap = *reinterpret_cast<const uchar* const>(ptr + id / 8);
        unsigned char pos = uchar(id % 8);
        isNull = (uchar(0x1 << pos) & bitmap) == 0;
    }
    const auto& p = mFieldMetaData[id];
    if (type != nullptr) {
        *type = p.first.type();
    }
    if (p.second < 0) {
        // we need to calc the position
        auto baseId = mSchema.fixedSizeFields().size();
        auto pos = mFieldMetaData[baseId].second;
        LOG_ASSERT(pos > 0, "Offset for first variable length field is smaller than 0");
        for (; baseId < id; ++baseId) {
            // we know, that now all fields are variable mLength - that means the first four bytes are always the
            // field size
            pos += *reinterpret_cast<const uint32_t* const>(ptr + pos) + sizeof(uint32_t);
        }
        return ptr + pos;
    } else {
        return ptr + p.second;
    }
}

bool Record::idOf(const crossbow::string& name, id_t& result) const {
    auto iter = mIdMap.find(name);
    if (iter == mIdMap.end()) return false;
    result = iter->second;
    return true;
}

char* Record::data(char* const ptr, Record::id_t id, bool& isNull, FieldType* type /* = nullptr */) {
    auto res = const_cast<const Record*>(this)->data(ptr, id, isNull, type);
    return const_cast<char*>(res);
}

Field Record::getField(char* const ptr, id_t id) {
    if (id >= mFieldMetaData.size()) {
        LOG_ERROR("Tried to read non-existent field");
        assert(false);
        return Field();
    }
    bool isNull;
    FieldType type;
    auto dPtr = data(ptr, id, isNull, &type);
    if (isNull)
        return Field();
    auto res = mFieldMetaData[id].first;
    res.mData = dPtr;
    return res;
}

Field Record::getField(char* const ptr, const crossbow::string& name)
{
    id_t i;
    if (!idOf(name, i)) {
        LOG_ERROR("Unknown Field %s", name);
        return Field();
    }
    return getField(ptr, i);
}

} // namespace store
} // namespace tell