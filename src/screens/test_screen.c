
#include "display.h"
#include "gui.h"
#include "gui/graph.h"

#include "sxml.h"
#include <esp_log.h>

#define BUFFER_MAXLEN 1024
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
extern const char test_gpx[];
const char* gpx = test_gpx;
size_t file_pos = 0;

/* Input XML text */
char buffer[BUFFER_MAXLEN];
uint32_t bufferlen = 0;

void ff_xml(sxml_t* parser)
{
    /*
     Example of how to reuse buffer array.
     Move unprocessed buffer content to start of array
    */
    bufferlen -= parser->bufferpos;
    memmove(buffer, buffer + parser->bufferpos, bufferlen);

    /*
     If your buffer is smaller than the size required to complete a token the parser will endlessly call SXML_ERROR_BUFFERDRY.
     You will most likely encounter this problem if you have XML comments longer than BUFFER_MAXLEN in size.
     SXML_CHARACTER solves this problem by dividing the data over multiple tokens, but other token types remain affected.
    */
    assert(bufferlen < BUFFER_MAXLEN);

    /* Fill remaining buffer with new data from file */
    const char* new_gpx = strncpy(buffer + bufferlen, gpx + file_pos, BUFFER_MAXLEN - bufferlen);
    // assert (0 < len);
    size_t str_len = strlen(new_gpx);
    file_pos += str_len;
    bufferlen += str_len;
}

void test_screen_create(const display_t* display)
{

    /* Output token table */
    sxmltok_t tokens[128];

    /* Parser object stores all data required for SXML to be reentrant */
    sxml_t parser;
    sxml_init(&parser);

    for (;;) {
        sxmlerr_t err = sxml_parse(&parser, buffer, bufferlen, tokens, COUNT(tokens));
        if (err == SXML_SUCCESS)
            break;

        switch (err) {
        case SXML_ERROR_TOKENSFULL: {
            /*
             Need to give parser more space for tokens to continue parsing.
             We choose here to reuse the existing token table once tokens have been processed.

             Example of some processing of the token data.
             Instead you might be interested in creating your own DOM structure
             or other processing of XML data useful to your application.
            */
            // print_prettyxml (buffer, tokens, parser.ntokens, &indent);

            /* Parser can now safely reuse all of the token table */
            parser.ntokens = 0;
            break;
        }

        case SXML_ERROR_BUFFERDRY: {
            /*
             Parser expects more XML data to continue parsing.
             We choose here to reuse the existing buffer array.
            */
            size_t len;

            /* Need to processs existing tokens before buffer is overwritten with new data */
            // print_prettyxml (buffer, tokens, parser.ntokens, &indent);
            char buf[255];
            for (uint32_t i = 0; i < parser.ntokens; i++) {
                strncpy(buf, buffer + tokens[i].startpos, tokens[i].endpos - tokens[i].startpos);
                buf[tokens[i].endpos - tokens[i].startpos] = 0;
                switch (tokens[i].type) {
                case SXML_STARTTAG:
                    ESP_LOGI("xml_starttag", "%s", buf);
                    break;
                case SXML_ENDTAG:
                    ESP_LOGI("xml_endtag", "%s", buf);
                    break;
                case SXML_CHARACTER:
                    ESP_LOGI("xml_character", "%s", buf);
                    break;
                case SXML_CDATA:
                    ESP_LOGI("xml_cdata", "%s", buf);
                    break;
                case SXML_INSTRUCTION:
                    ESP_LOGI("xml_instruction", "%s", buf);
                    break;
                case SXML_DOCTYPE:
                    ESP_LOGI("xml_doctype", "%s", buf);
                    break;
                case SXML_COMMENT:
                    ESP_LOGI("xml_starttag", "%s", buf);
                    break;
                default:
                    break;
                }
                vPortYield();
            }

            parser.ntokens = 0;

            ff_xml(&parser);
            /* For error reporting */
            // lineno+= count_lines(buffer, parser.bufferpos);

            /* Parser will now have to read from beginning of buffer to contiue */
            parser.bufferpos = 0;
            break;
        }

        case SXML_ERROR_XMLINVALID: {
            char fmt[8];

            /* Example of some simple error reporting */
            // lineno+= count_lines (buffer, parser.bufferpos);
            // fprintf(stderr, "Error while parsing line\n");

            /* Print out contents of line containing the error */
            // sprintf (fmt, "%%.%ds", MIN (bufferlen - parser.bufferpos, 72));
            //  fprintf (stderr, fmt, buffer + parser.bufferpos);
            ESP_LOGI("xml_error", "%.3s", buffer + parser.bufferpos);

            // abort();
            break;
        }

        default:
            assert(0);
            break;
        }
        vPortYield();
    }

    graph_t* graph = graph_create(0, display->size.height - 45, display->size.width, 45, tokens, 2, &f8x8);
    graph_set_range(graph, 0, 10);
    graph->current_position = 4;

    add_to_render_pipeline(graph_renderer, graph, RL_GUI_ELEMENTS);
}
