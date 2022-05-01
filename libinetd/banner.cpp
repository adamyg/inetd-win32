//
//  connection banner
//

namespace {
template<typename Pred>
bool load_file(const char *filename, Pred &pred)
{
	std::ifstream stream(filename, std::ios::in|std::ios::binary);
	if (stream.fail()) {
		parser.serverr("unable to open source <%s>", filename);
		return false;
	}

       	char buffer[1024]; // <MTU
        while (!std::eof(stream)) {
               	stream.read(buffer, sizeof(buffer));
                auto count = stream.gcount();
                if (count) {
                    //
                    continue;
                }
                break;
	}
	return true;
}
};


//end