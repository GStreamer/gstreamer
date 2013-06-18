<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>
  <xsl:output method='text'/>
  <xsl:template xmlns:xi='http://www.w3.org/2003/XInclude' match='ulink'>
    <xsl:value-of select='@url'/><xsl:text>&#xa;</xsl:text>
  </xsl:template>
  <xsl:template match='text()|@*'/>
</xsl:stylesheet>
