#include <stdio.h>  // for printf
#include <SQLAPI.h> // main SQLAPI++ header

// forwards
SAString ReadWholeFile(const char *sFilename);
size_t  FromFileWriter(
	SAPieceType_t &ePieceType,
	void *pBuf, size_t nLen, void *pAddlData);

int main(int argc, char* argv[])
{
    SAConnection con;		// connection object
	// command object
	// update fblob field of our test table
    SACommand cmd(&con, 
		_TSA("Update test_tbl set fblob = :fblob where fid =:1"));
    
    try
    {
        // connect to database (Oracle in our example)
        con.Connect(_TSA("test"), _TSA("tester"), _TSA("tester"), SA_Oracle_Client);
		
        // use first method of Long(Lob) binding - as a whole
        cmd.Param(1).setAsLong() = 1;	// fid
        cmd.Param(_TSA("fblob")).setAsBLob() = ReadWholeFile("test.doc");
        // update first row
        cmd.Execute();
		
        // use second method of binding - user defined callbacks
        cmd.Param(1).setAsLong() = 2;
		cmd.Param(_TSA("fblob")).setAsBLob(
			FromFileWriter,	// our callback to provide blob data from file
			10*1024,		// desired size of each piece
			(void*)"test.doc");	// additional param, file name in our case
        // update second row
        cmd.Execute();	// at that moment Library will call user callback when needed
		
        // commit changes on success
        con.Commit();
		
        printf("Blob parameter bound, rows updated!\n");
    }
    catch(SAException &x)
    {
        // SAConnection::Rollback()
        // can also throw an exception
        // (if a network error for example),
        // we will be ready
        try
        {
            // on error rollback changes
            con.Rollback();
        }
        catch(SAException &)
        {
        }
        // print error message
        printf("%s\n", x.ErrText().GetMultiByteChars());
    }
    
    return 0;
}

SAString ReadWholeFile(const char *sFilename)
{
	SAString s;
	char sBuf[32*1024];
	FILE *pFile = fopen(sFilename, "rb");

	if (!pFile)
	{
		SAString sName(sFilename);
		SAException::throwUserException(-1,
			_TSA("Error opening file '%s'\n"), (const SAChar*)sName);
	}

	do
	{
		size_t nRead = fread(sBuf, 1, sizeof(sBuf), pFile);
		s += SAString(sBuf, nRead);
	}
	while(!feof(pFile));
	
	fclose(pFile);
	
	return s;
}

static FILE *pFile = NULL;
size_t nTotalBound;
size_t FromFileWriter(
	SAPieceType_t &ePieceType,
	void *pBuf, size_t nLen, void *pAddlData)
{
	if(ePieceType == SA_FirstPiece)
	{
		const char *sFilename = (const char *)pAddlData;
		pFile = fopen(sFilename, "rb");
		if (!pFile)
		{
			SAString sName(sFilename);
			SAException::throwUserException(-1, _TSA("Can not open file '%s'"), (const SAChar*)sName);
		}
		
		nTotalBound = 0;
	}
	
	size_t nRead = fread(pBuf, 1, nLen, pFile);
	nTotalBound += nRead;
	// show progress
	printf("%d bytes of file bound\n", nTotalBound);
	
	if(feof(pFile))
	{
		ePieceType = SA_LastPiece;
		fclose(pFile);
		pFile = NULL;
	}
	
	return nRead;
}
