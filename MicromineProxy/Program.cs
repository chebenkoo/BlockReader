using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using MicromineWrapper;

namespace MicromineProxy
{
    class Program
    {
        static int Main(string[] args)
        {
            if (args.Length < 1) return 1;
            string filePath = args[0];
            bool fieldsOnly = args.Contains("--fields-only");

            try
            {
                using (var dbText = new DbTextWrapper(filePath))
                {
                    if (dbText == null) return 1;
                    int totalFields = dbText.Structure.GetFieldCount();
                    int totalRecords = dbText.GetTotalRecords();

                    if (fieldsOnly)
                    {
                        for (int i = 0; i < totalFields; i++)
                            Console.WriteLine(dbText.Structure.GetField(i).GetName());
                        return 0;
                    }

                    using (var stdout = Console.OpenStandardOutput())
                    using (var writer = new BinaryWriter(stdout))
                    {
                        // Header
                        writer.Write(Encoding.ASCII.GetBytes("MMBM"));
                        writer.Write(totalRecords);
                        writer.Write(totalFields);

                        for (int i = 0; i < totalFields; i++)
                        {
                            var nameBytes = Encoding.UTF8.GetBytes(dbText.Structure.GetField(i).GetName());
                            writer.Write(nameBytes.Length);
                            writer.Write(nameBytes);
                        }

                        // Data Loop
                        for (int i = 0; i < totalRecords; i++)
                        {
                            // PER-RECORD SYNC MARKER
                            writer.Write((uint)0xAA55AA55);

                            if (dbText.LoadRecord(i))
                            {
                                for (int f = 0; f < totalFields; f++)
                                {
                                    try { writer.Write(dbText.GetDouble(f)); }
                                    catch { writer.Write(0.0); }
                                }
                            }
                            else
                            {
                                for (int f = 0; f < totalFields; f++) writer.Write(0.0);
                            }
                        }
                        writer.Flush();
                    }
                }
                return 0;
            }
            catch { return 1; }
        }
    }
}
